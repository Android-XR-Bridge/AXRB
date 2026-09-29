#!/system/bin/sh
export BERBERIS_MODE=interpret-only
export LD_PRELOAD=/data/local/tmp/axrb-dynarmic-probe/libaxrb_dynarmic_bridge.so
program="$1"
shift
for argument in "$@"; do
    case "$argument" in
        --nice-name=*) package="${argument#--nice-name=}"; cd "/data/user/0/${package%%:*}" || exit 1 ;;
    esac
done
exec "$program" -Xforce-nb-testing -XX:NativeBridge=libndk_translation.so "$@"
