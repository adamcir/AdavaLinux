# Installer Dynamic String Config Design

**Goal:** Remove all fixed-size text buffers from the AdavaLinux installer configuration and input flow so every text field uses dynamically sized storage.

## Chosen Approach

Replace fixed-size character arrays in both the frontend installer state and backend installer configuration with `char *` fields. Centralize string ownership behind small helpers that duplicate, replace, and free strings explicitly.

## Scope

- `tools/installer/main.c`
- `tools/installer/install.c`
- `tools/installer/install.h`
- `tools/installer/installer_logic.c`
- `tools/installer/installer_logic.h`
- `tools/installer/tests/test_logic.c`

## Behavior

- All installer text fields become dynamically allocated.
- Input fields grow with `realloc` as the user types.
- Default values such as hostname and boot mode are set through string helpers instead of copied into fixed arrays.
- Backend environment loading duplicates environment values into owned strings.
- Backend cleanup frees all allocated text fields before exit.

## Constraints

- Preserve current installer flow and validation behavior.
- Keep `NULL` handling safe everywhere strings are displayed, compared, or exported to environment variables.
- Avoid silent truncation in the UI and backend config loading.
- Keep helper usage simple enough that ownership is obvious at each call site.

## Testing

- Add tests for the dynamic input growth helper.
- Keep existing logic tests green.
- Rebuild the installer and backend after the migration.
