#include "rf/motion_ingest.h"
#include "v02/motion_engine.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace sentinel;

namespace {

std::string pairedBody(const char* session, unsigned messageId,
                       unsigned long long t0Ns, unsigned long long deltaNs,
                       const char* accelX = "0.1",
                       const char* gravityZ = "9.80665") {
  const unsigned long long t1Ns = t0Ns + deltaNs;
  return std::string("{\"messageId\":") + std::to_string(messageId) +
         ",\"sessionId\":\"" + session +
         "\",\"deviceId\":\"iphone-12\",\"payload\":[" +
         "{\"name\":\"accelerometer\",\"time\":" + std::to_string(t0Ns) +
         ",\"values\":{\"x\":" + accelX + ",\"y\":0.2,\"z\":0.3}}," +
         "{\"name\":\"gravity\",\"time\":" + std::to_string(t0Ns) +
         ",\"values\":{\"x\":0,\"y\":0,\"z\":" + gravityZ + "}}," +
         "{\"name\":\"accelerometer\",\"time\":" + std::to_string(t1Ns) +
         ",\"values\":{\"x\":0.4,\"y\":0.5,\"z\":0.6}}," +
         "{\"name\":\"gravity\",\"time\":" + std::to_string(t1Ns) +
         ",\"values\":{\"x\":0,\"y\":0,\"z\":" + gravityZ + "}}]}";
}

void expectStatus(const rf::MotionIngestResult& result, int code,
                  rf::MotionIngestStatus status) {
  static int check = 0;
  ++check;
  if (result.httpCode != code) {
    std::fprintf(stderr, "check %d: expected HTTP %d, got %d (%s)\n", check,
                 code, result.httpCode, result.error);
  }
  assert(result.httpCode == code);
  assert(result.status == status);
}

}  // namespace

int main() {
  v02::MotionEngine engine;
  engine.begin(v02::MotionThresholds{}, 1000);
  rf::MotionIngest ingest;
  ingest.attach(engine);

  const std::string body = pairedBody("session-a", 0, 1698501144401773000ULL,
                                      50000000ULL);
  const auto feed = [&ingest](const std::string& text, uint32_t now) {
    return ingest.ingest(text.c_str(), text.size(), now);
  };

  expectStatus(feed(body, 1000), 403,
               rf::MotionIngestStatus::Unauthorized);
  rf::MotionIngestSnapshot snapshot = ingest.snapshot(1000);
  assert(std::strcmp(snapshot.pendingDeviceId, "iphone-12") == 0);
  assert(!snapshot.authorized);
  assert(snapshot.lastHttpCode == 403);
  assert(std::strstr(snapshot.lastError, "not authorized") != nullptr);
  assert(engine.snapshot().sampleCount == 0);

  assert(ingest.authorize("iphone-12"));
  assert(!ingest.authorize("bad id with spaces"));
  const rf::MotionIngestResult accepted = feed(body, 1000);
  expectStatus(accepted, 200, rf::MotionIngestStatus::Ok);
  assert(accepted.accepted == 2);
  assert(accepted.skipped == 0);
  assert(engine.snapshot().sampleCount == 2);
  assert(engine.snapshot().source == v02::AccelSource::NativeClient);
  assert(ingest.snapshot(1000).lastHttpCode == 200);

  // Duplicate and out-of-order batches are rejected, never replayed.
  expectStatus(feed(body, 1100), 409,
               rf::MotionIngestStatus::StaleMessage);

  // Missing gravity is never replaced by an invented vector.
  const std::string noGravity =
      "{\"messageId\":1,\"sessionId\":\"session-b\","
      "\"deviceId\":\"iphone-12\",\"payload\":["
      "{\"name\":\"accelerometer\",\"time\":1698501144501773000,"
      "\"values\":{\"x\":1,\"y\":2,\"z\":3}}]}";
  expectStatus(feed(noGravity, 1200), 422,
               rf::MotionIngestStatus::NoPairedSamples);
  assert(engine.snapshot().sampleCount == 2);

  // Non-finite samples are rejected.
  const std::string invalid = pairedBody("session-c", 2,
                                         1698501144601773000ULL,
                                         50000000ULL, "999999", "9.80665");
  const rf::MotionIngestResult invalidResult = feed(invalid, 1300);
  expectStatus(invalidResult, 200, rf::MotionIngestStatus::Ok);
  assert(invalidResult.accepted == 1);
  assert(invalidResult.skipped == 0);
  assert(engine.snapshot().sampleCount == 3);

  // Interval control: a 5 ms pair is skipped when 20 ms is configured.
  const std::string tooFast = pairedBody("session-d", 3,
                                         1698501144701773000ULL,
                                         5000000ULL);
  const rf::MotionIngestResult throttled = feed(tooFast, 1400);
  expectStatus(throttled, 200, rf::MotionIngestStatus::Ok);
  assert(throttled.accepted == 1);
  assert(throttled.skipped == 1);

  // Simulation cannot enter through the native endpoint.
  const std::string simulated =
      "{\"simulated\":true,\"messageId\":4,\"sessionId\":\"session-e\","
      "\"deviceId\":\"iphone-12\",\"payload\":[]}";
  expectStatus(feed(simulated, 1500), 400,
               rf::MotionIngestStatus::InvalidPayload);

  const std::string oversized(20000, 'x');
  expectStatus(ingest.ingest(oversized.c_str(), oversized.size(), 1600), 413,
               rf::MotionIngestStatus::BodyTooLarge);

  snapshot = ingest.snapshot(1500);
  assert(snapshot.authorized);
  assert(snapshot.lastMessageId == 3);
  assert(snapshot.acceptedSamples == 4);
  assert(snapshot.skippedSamples == 2);
  assert(!snapshot.signalLost);

  engine.update(engine.snapshot().lastSampleMs +
                engine.thresholds().timeoutMs + 1);
  snapshot = ingest.snapshot(engine.snapshot().lastSampleMs +
                             engine.thresholds().timeoutMs + 1);
  assert(snapshot.signalLost);

  ingest.clearAuthorization();
  assert(!ingest.snapshot(3000).authorized);
  std::puts("test_motion_ingest: ALL TESTS PASSED");
  return 0;
}
