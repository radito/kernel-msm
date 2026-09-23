#!/usr/bin/env bash

set -euo pipefail

variant="${1:?usage: $0 <diag-link-only|diag-init-only|diag-backports-only|diag-backports-no-seccomp>}"
baseline_commit="ded58d27f72c79d67e634d4e11d71c93245dc99f"

case "$variant" in
  diag-link-only|diag-init-only|diag-backports-only|diag-backports-no-seccomp)
    ;;
  *)
    echo "unsupported diagnostic variant: $variant" >&2
    exit 2
    ;;
esac

hook_paths=(
  drivers/input/input.c
  fs/exec.c
  fs/open.c
  fs/read_write.c
  fs/stat.c
  kernel/reboot.c
)

patch_file="${RUNNER_TEMP:-/tmp}/ksu-manual-hooks.patch"
git diff --binary "$baseline_commit"..HEAD -- "${hook_paths[@]}" > "$patch_file"
test -s "$patch_file"
git apply --reverse --check "$patch_file"
git apply --reverse "$patch_file"

hook_checks=(
  drivers/input/input.c:ksu_handle_input_handle_event
  fs/exec.c:ksu_handle_execveat
  fs/open.c:ksu_handle_faccessat
  fs/read_write.c:ksu_handle_vfs_read
  fs/stat.c:ksu_handle_stat
  kernel/reboot.c:ksu_handle_sys_reboot
)

for hook_check in "${hook_checks[@]}"; do
  path="${hook_check%%:*}"
  symbol="${hook_check#*:}"
  if grep -Fq "$symbol" "$path"; then
    echo "manual KernelSU hook still present in $path" >&2
    exit 1
  fi
done

if [[ "$variant" == "diag-link-only" ]]; then
  init_file="KernelSU-Next/kernel/core/init.c"
  marker="CI diagnostic: KernelSU runtime initialization disabled"

  perl -0pi -e \
    's/int __init kernelsu_init\(void\)\n\{/int __init kernelsu_init(void)\n{\n\t\/\* CI diagnostic: KernelSU runtime initialization disabled \*\/\n\treturn 0;/' \
    "$init_file"
  grep -Fq "$marker" "$init_file"
fi

if [[ "$variant" == diag-backports-* ]]; then
  kbuild_file="KernelSU-Next/kernel/Kbuild"
  marker="CI diagnostic: KernelSU objects not linked"

  perl -0pi -e \
    's/^obj-\$\(CONFIG_KSU\) \+= kernelsu\.o$/# CI diagnostic: KernelSU objects not linked/m' \
    "$kbuild_file"
  grep -Fq "$marker" "$kbuild_file"
fi

if [[ "$variant" == "diag-backports-no-seccomp" ]]; then
  seccomp_file="include/linux/seccomp.h"
  marker="CI diagnostic marker: atomic_t filter_count;"

  perl -0pi -e \
    's{(#endif /\* _LINUX_SECCOMP_H \*/)}{/* CI diagnostic marker: atomic_t filter_count; */\n$1}' \
    "$seccomp_file"
  grep -Fq "$marker" "$seccomp_file"
fi

printf 'diagnostic_variant=%s\n' "$variant"
printf 'manual_hook_calls=disabled\n'
case "$variant" in
  diag-link-only)
    printf 'kernelsu_objects=linked\n'
    printf 'kernelsu_init=disabled\n'
    ;;
  diag-init-only)
    printf 'kernelsu_objects=linked\n'
    printf 'kernelsu_init=enabled\n'
    ;;
  diag-backports-only)
    printf 'kernelsu_objects=not-linked\n'
    printf 'kernelsu_init=not-linked\n'
    printf 'seccomp_backport=enabled\n'
    ;;
  diag-backports-no-seccomp)
    printf 'kernelsu_objects=not-linked\n'
    printf 'kernelsu_init=not-linked\n'
    printf 'seccomp_backport=disabled\n'
    ;;
esac
