#pragma once
#include <cmath>
#include <stddef.h>
namespace sentinel { namespace rf {
inline bool validRssi(double rssi) { return std::isfinite(rssi) && rssi >= -100 && rssi < 0; }
struct CalibrationPoint { double distanceM, rssiDbm; };
class RssiFilter {
 public:
  bool feed(double rssi) { if (!validRssi(rssi)) { reset(); return false; }
    value_ = ready_ ? 0.75 * value_ + 0.25 * rssi : rssi; ready_ = true; return true; }
  void reset() { ready_ = false; }
  bool ready() const { return ready_; }
  double value() const { return ready_ ? value_ : NAN; }
 private: bool ready_ = false; double value_ = 0;
};
class DistanceEstimator {
 public:
  // Fit RSSI = intercept - 10*n*log10(d/1m). Invalid fits leave prior model intact.
  bool calibrate(const CalibrationPoint* points, size_t count) {
    if (!points || count < 2) return false;
    double sx=0, sy=0, sxx=0, sxy=0;
    for (size_t i=0; i<count; ++i) {
      if (!std::isfinite(points[i].distanceM) || points[i].distanceM <= 0 ||
          !validRssi(points[i].rssiDbm)) return false;
      const double x=std::log10(points[i].distanceM), y=points[i].rssiDbm;
      sx+=x; sy+=y; sxx+=x*x; sxy+=x*y;
    }
    const double denominator=count*sxx-sx*sx;
    if (denominator < 1e-8) return false;
    const double slope=(count*sxy-sx*sy)/denominator;
    const double exponent=-slope/10, intercept=(sy-slope*sx)/count;
    if (!std::isfinite(exponent) || exponent < 0.5 || exponent > 8 || !validRssi(intercept)) return false;
    exponent_=exponent; reference_=intercept; calibrated_=true; return true;
  }
  double estimate(double rssi) const {
    if (!calibrated_ || !validRssi(rssi)) return NAN;
    const double d=std::pow(10.0, (reference_-rssi)/(10*exponent_));
    return std::isfinite(d) && d>0 ? d : NAN;
  }
  static double absoluteError(double estimate, double actual) {
    return std::isfinite(estimate) && estimate>0 && std::isfinite(actual) && actual>=0
      ? std::fabs(estimate-actual) : NAN;
  }
  bool calibrated() const { return calibrated_; }
  double exponent() const { return exponent_; }
  double referenceRssi() const { return reference_; }
 private: bool calibrated_=false; double exponent_=0, reference_=0;
};
} }
