# shellcheck shell=sh
# Issue #28. See the comment in gate_port.rb: a gate must know its port is
# free before it binds, or a sequencing problem is reported as a gate failure.
# Sourced, not executed.

gate_port() {
    _gp_port=$1
    _gp_name=${2:-$(basename "$0")}
    _gp_timeout=${BLIT_GATE_PORT_TIMEOUT:-15}
    _gp_deadline=$(($(date +%s) + _gp_timeout))
    while :; do
        if ! (exec 3<>"/dev/tcp/127.0.0.1/$_gp_port") 2>/dev/null; then
            printf '%s\n' "$_gp_port"
            return 0
        fi
        exec 3>&- 2>/dev/null || true
        if [ "$(date +%s)" -gt "$_gp_deadline" ]; then
            _gp_holder=$(ss -ltnp 2>/dev/null | grep ":$_gp_port " || true)
            printf '%s: TCP port %s is still serving after %ss, so this gate cannot bind it. This is a port conflict, not a failure of what the gate asserts. %s\n' \
                "$_gp_name" "$_gp_port" "$_gp_timeout" "$_gp_holder" >&2
            exit 1
        fi
        sleep 1
    done
}
