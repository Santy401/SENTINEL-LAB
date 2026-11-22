#!/usr/bin/env bash
# Installs the gateway as a systemd *user* service (no root needed).
#   deploy/install-gateway-service.sh            # install + enable + start
#   deploy/install-gateway-service.sh --no-start # install only
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CONF="$HOME/.config/sentinel-lab"
UNIT_DIR="$HOME/.config/systemd/user"
START=1
[[ "${1:-}" == "--no-start" ]] && START=0

mkdir -p "$CONF" "$UNIT_DIR" "$HOME/.local/share/sentinel-lab"
chmod 700 "$CONF"
if [[ ! -f "$CONF/gateway.env" ]]; then
  # systemd does not expand %h inside EnvironmentFile values: resolve it here.
  sed -e "s#%h#$HOME#g" -e "s#^SENTINEL_REPO=.*#SENTINEL_REPO=$REPO#" \
    "$REPO/deploy/gateway.env.example" > "$CONF/gateway.env"
  chmod 600 "$CONF/gateway.env"
  echo "==> created $CONF/gateway.env (review it)"
fi
for f in realtime-token alert-secret; do
  if [[ -e "$CONF/$f" ]]; then
    perms=$(stat -c '%a' "$CONF/$f")
    [[ "$perms" == "600" ]] || { echo "FAIL: $CONF/$f must be chmod 600 (is $perms)"; exit 1; }
  else
    echo "WARN: $CONF/$f missing"
  fi
done
install -m 644 "$REPO/deploy/sentinel-gateway.service" "$UNIT_DIR/sentinel-gateway.service"
systemctl --user daemon-reload
if [[ "$START" == 1 ]]; then
  systemctl --user enable --now sentinel-gateway.service
  systemctl --user --no-pager status sentinel-gateway.service | head -5 || true
else
  echo "==> installed; start with: systemctl --user enable --now sentinel-gateway.service"
fi
echo "==> logs: journalctl --user -u sentinel-gateway -f"
