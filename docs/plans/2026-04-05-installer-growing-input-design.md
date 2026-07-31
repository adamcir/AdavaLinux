# Installer Growing Input Design

**Goal:** Make the installer text input field keep the cursor on the actual typing position by growing to multiple lines when the text reaches the visible width, and shrinking again when trailing lines become empty after deletion.

## Chosen Approach

Keep the existing `ncurses` dialog flow in `tools/installer/main.c`, but replace the fixed single-line input rendering with a small layout calculation that derives:

- wrapped line count for the current buffer
- on-screen cursor row and column
- dialog height needed for the current field and optional help text

Wrapping is character-based rather than word-based. This keeps cursor placement exact for hostnames, usernames, passwords, partition sizes, and destructive confirmation text.

## Behavior

- The input field starts as one visible row.
- When the typed content exceeds the field width, the content wraps onto the next row.
- The cursor is drawn at the exact wrapped row and column for the current buffer length.
- Backspace removes characters from the end as today.
- If deletion clears the last wrapped row, the field shrinks by one row and the dialog height is reduced accordingly.
- The prompt and footer stay in their current positions relative to the field.

## Constraints

- Preserve the current append-only editing model.
- Keep password masking behavior unchanged.
- Avoid introducing horizontal scrolling or left/right in-field navigation in this change.
- Keep dialogs centered after height changes.

## Testing

- Add a pure helper test for wrapped line count and cursor placement.
- Verify that text exactly filling one row keeps one line until the next character is typed.
- Verify that deleting back below the boundary shrinks the line count again.
