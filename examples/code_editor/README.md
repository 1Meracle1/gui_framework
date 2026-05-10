# Code Editor Keybindings

This example editor combines modal normal-mode bindings with common Ctrl
shortcuts. Normal-mode sequences use a Neovim/Helix style shape, while direct
Ctrl bindings follow familiar desktop and VS Code conventions.

## Notation

- `A B` means press `A`, release it, then press `B`.
- `Ctrl+K` means hold Ctrl while pressing `K`.
- `Shift+motion` extends a selection for arrow, Home, and End navigation.
- Printable normal-mode keys are case-sensitive: `g d` and `g D` are different
  commands.
- `Space` is the leader key in normal mode and in the file tree.

## Global Code Pane Keys

These keys work in the code pane before normal/insert-mode dispatch unless
noted otherwise.

| Key | Action |
| --- | --- |
| `Esc` | Leave insert mode, cancel pending key prefixes, clear selection, and clear extra cursors when already in normal mode with no selection. In a file tree edit, it cancels the edit. |
| `Ctrl+=`, `Ctrl++` | Increase editor font size. |
| `Ctrl+-` | Decrease editor font size. |
| `Ctrl+S` | Save the current file. Untitled scratch files open the save-path dialog. |
| `Ctrl+N` | Create a new scratch buffer. |
| `Ctrl+W` | Request closing the current buffer. Dirty buffers show the unsaved-close popup. |
| `Ctrl+A` | Select the whole buffer. |
| `Ctrl+C` | Copy the active selection. |
| `Ctrl+V` | Paste clipboard text at the cursor or active selections. |
| `Ctrl+Z` | Undo the latest text edit in the current buffer. |
| `Ctrl+/` | Toggle `//` line comments on the current line or selected lines. |
| `Ctrl+Space` | Request LSP completion at the cursor. |
| `Ctrl+D` | Scroll the focused code split down by half a page. |
| `Ctrl+U` | Scroll the focused code split up by half a page. |
| `Ctrl+Alt+Up` | Add an extra cursor on the line above the current cursor set. |
| `Ctrl+Alt+Down` | Add an extra cursor on the line below the current cursor set. |

## Shared Code Navigation

These navigation keys work in both insert and normal mode in the code pane.
Holding `Shift` extends the selection. In normal visual modes, the same motions
also extend the active visual selection.

| Key | Action |
| --- | --- |
| `Left`, `Right` | Move one character left or right. Without selection extension, these collapse an active selection to its start or end. |
| `Up`, `Down` | Move one visible line up or down, preserving preferred column where possible. |
| `Ctrl+Left`, `Ctrl+Right` | Move to the previous or next word boundary. |
| `Home` | Move to the start of the current line. With multiple cursors, moves all cursors to line starts. |
| `End` | Move to the end of the current line. With multiple cursors, moves all cursors to line ends. |
| `Ctrl+Home` | Move to the start of the buffer. If extra cursors exist and selection is not active, it first clears them. |
| `Ctrl+End` | Move to the end of the buffer. If extra cursors exist and selection is not active, it first clears them. |

## Insert Mode

Insert mode is entered from normal mode with commands such as `i`, `a`, `o`,
or `c`. Printable text inserts directly into the buffer. Typing alphanumeric
characters, `_`, `.`, `>`, or `:` also requests LSP completion.

| Key | Action |
| --- | --- |
| `Esc` | Return to normal mode. |
| `Enter` | Insert a newline. |
| `Tab` | Insert four spaces. |
| `Backspace` | Delete the previous character, or delete the selection. |
| `Ctrl+Backspace` | Delete the previous word, or delete the selection. |
| `Delete` | Delete the next character, or delete the selection. |
| `Ctrl+Delete` | Delete the next word, or delete the selection. |

## Normal Mode Motion

| Key | Action |
| --- | --- |
| `h`, `j`, `k`, `l` | Move left, down, up, or right. |
| `w`, `b`, `e` | Move to the next word, previous word, or end of word. |
| `W`, `B`, `E` | Move by big-word boundaries. Big words use broader whitespace-separated spans. |
| `0` | Move to the start of the current line. |
| `$` | Move to the end of the current line. |
| `g g` | Move to the start of the buffer. |
| `G` | Move to the last line of the buffer. |
| `<number> G` | Move to a 1-based line number, for example `42 G`. The number prefix is only used by `G`. |
| `z z` | Center the cursor line in the focused split. |

## Normal Mode Selection

| Key | Action |
| --- | --- |
| `v` | Toggle character-wise visual selection from the cursor. Motions extend the selection. |
| `V` | Toggle line-wise visual selection from the cursor line. Motions extend whole-line selection. |
| `,` | Clear the active selection and remove extra cursors. |
| `Esc` | Clear selection and pending prefixes. If no selection is active, it also clears extra cursors. |
| `>` | Indent the selected lines by four spaces. |
| `<` | Unindent the selected lines by one tab or up to four leading spaces. |

## Normal Mode Editing

| Key | Action |
| --- | --- |
| `i` | Enter insert mode at the cursor, or at the start of the active selection. |
| `I` | Enter insert mode at column 0 of the current line or selection start line. With extra cursors, moves all cursors to line starts first. |
| `a` | Enter insert mode after the cursor, or at the end of the active selection. With extra cursors, advances all cursors first. |
| `A` | Enter insert mode at the end of the current line or selection end line. With extra cursors, moves all cursors to line ends first. |
| `o` | Open a new line below the current line or selection and enter insert mode. |
| `O` | Open a new line above the current line or selection and enter insert mode. |
| `c` | Change text. With a selection, yanks and deletes it, then enters insert mode. Without a selection, deletes the current character if possible, then enters insert mode. |
| `Alt+c` | Change the selection without copying it to the clipboard. |
| `d` | With a selection, yank and delete the selection. Without a selection, starts the `d d` pending sequence. |
| `d d` | Delete the current line. |
| `Alt+d` | Delete the active selection without copying it to the clipboard. |
| `x` | Delete the current character. In visual selection, deletes the active selection. |
| `Delete` | Delete the next character, or delete the selection. |
| `Ctrl+Delete` | Delete the next word, or delete the selection. |
| `r <char>` | Replace the selected text or current character with the next typed character. |
| `R` | Replace the active selection with clipboard text. |
| `y` | Copy the active selection. |
| `p` | Paste after the active selection or after the cursor. |
| `P` | Paste before the active selection or at the cursor. |
| `u` | Undo the latest text edit. |
| `U` | Redo the latest undone text edit. |
| `C` | Add an extra cursor on the next line. |
| `~` | Switch ASCII case in the active selection. |
| `` ` `` | Lowercase ASCII letters in the active selection. |
| `Alt+\`` | Uppercase ASCII letters in the active selection. |

## Search

| Key | Action |
| --- | --- |
| `/` | Open current-buffer search in the bottom bar. The match updates as the query changes. |
| `Enter` in current-buffer search | Accept the current query and close the search input. |
| `Esc` in current-buffer search | Close the search input. |
| `*` | Search for the currently selected text. |
| `n` | Jump to the next match for the last current-buffer search. |
| `N` | Jump to the previous match for the last current-buffer search. |
| `Space /` | Open workspace global search. Press `Enter` after typing a query to populate the result picker. |

## LSP And Code Intelligence

| Key | Action |
| --- | --- |
| `Ctrl+Space` | Request completions at the cursor. |
| `K` | Request hover information at the cursor. The hover popup closes on any key press, text input, or outside mouse click. |
| `g d` | Request go-to definition. Results open through the jump/result picker. |
| `g D` | Request go-to declaration. Results open through the jump/result picker. |
| `g r` | Request references. Results open through the jump/result picker. |
| `Space s` | Request document symbols. |
| `Space S` | Request workspace symbols. |
| `Space d` | Open diagnostics for the current file. |
| `Space D` | Open workspace diagnostics. |
| `Space r n`, `Space c n` | Open LSP rename for the symbol at the cursor. |
| `Space r a`, `Space c a` | Request code actions at the cursor. |
| `Space r s`, `Space c s` | Request document symbols. |
| `Space r j`, `Space c j` | Open the jump list. |

## Scope Folding

Folding is backed by LSP `textDocument/foldingRange`. When no usable folding
range is cached, close/toggle commands request ranges from the language server.
The command becomes effective once the LSP response has arrived.

| Key | Action |
| --- | --- |
| `z a` | Toggle the current scope fold. If the cursor line is already folded, open it; otherwise fold the smallest LSP range that contains the cursor. |
| `z c` | Close the current scope. It folds the smallest LSP range containing the cursor. |
| `z o` | Open the fold at the cursor line. If the cursor is inside hidden folded text, the containing folded header is opened. |
| `z M` | Close all known LSP fold ranges in the current buffer. Nested folds hidden by a parent fold are skipped. |
| `z R` | Open all folds in the current buffer. |
| `z z` | Center the cursor line. This is grouped under `z` to match modal editor convention. |

The line-number gutter also shows `-` for open foldable lines and `+` for
folded lines. Clicking that marker toggles the fold for that line.

## Leader Keys

| Key | Action |
| --- | --- |
| `Space e` | Toggle the filesystem sidebar. |
| `Space g` | Toggle the Git sidebar. |
| `Space f` | Open the indexed file picker. |
| `Space b` | Open the open-buffer picker. |
| `Space d` | Open current-file diagnostics. |
| `Space D` | Open workspace diagnostics. |
| `Space /` | Open workspace global search. |
| `Space j` | Open the jump history list. |
| `Space o` | Jump to the previous recorded location. |
| `Space i` | Jump to the next recorded location. |
| `Space n` | Create a new scratch buffer. |
| `Space w` | Start the window/split prefix. |
| `Space s` | Request document symbols. |
| `Space S` | Request workspace symbols. |
| `Space r`, `Space c` | Start the LSP/code-action prefix. |

## Window And Split Prefix

These are entered after `Space w`.

| Key | Action |
| --- | --- |
| `v` | Split the focused code pane vertically. |
| `s` | Split the focused code pane horizontally. |
| `h`, `j`, `k`, `l` | Focus the split to the left, below, above, or right. |
| `H`, `J`, `K`, `L` | Swap the focused split with the split to the left, below, above, or right. |
| `q` | Close the focused split. |

## File, Buffer, Jump, And Result Pickers

These keys apply to file search, buffer search, jump history, diagnostics,
references, symbols, code actions, and global-search result pickers.

| Key | Action |
| --- | --- |
| Printable text | Filter the picker query when the picker has an editable query field. Global-search results reuse the submitted query. |
| `Up` | Move the picker selection up. |
| `Down` | Move the picker selection down. |
| `Enter` | Open or apply the selected item. |
| `Esc` | Close the picker. |

## LSP Popups

| Key | Action |
| --- | --- |
| `Up` | Move selection up in completion, location, code-action, or symbol popups. |
| `Down` | Move selection down in completion, location, code-action, or symbol popups. |
| `Enter` | Accept the selected completion, location, code action, symbol, or rename text. |
| `Tab` | Accept the selected completion, location, code action, symbol, or rename text. |
| `Esc` | Close the active LSP popup. |
| Printable text in completion popup | Close the completion popup and let the typed character continue through to normal insert handling. |

## Command Line

| Key | Action |
| --- | --- |
| `:` | Open the command line. |
| `Tab` | Complete the current command name. If the text already equals the selected command, Tab advances to the next command. |
| `Enter` | Run the command. Command names and aliases are matched case-insensitively. |
| `Esc` | Close the command line without running it. |

### Commands

| Command | Alias | Action |
| --- | --- | --- |
| `write` | `w` | Save the current file. |
| `quit` | `q` | Close the focused split. |
| `new` | `n` | Create a scratch buffer. |
| `write-quit` | `wq` | Save and close the current buffer. |
| `quit-force` | `q!` | Close the current buffer without saving. |
| `buffer-close` | `bc` | Close the current buffer. |
| `open` | `o` | Open the indexed file picker. |
| `toggle-sidebar` | `tree` | Toggle the filesystem sidebar. |
| `format` | `fmt` | Request LSP formatting for the current C/C++ file. |
| `search` | `s` | Open current-buffer search. |
| `symbols` | `sym` | Request document symbols. |
| `fold-toggle` | `za` | Toggle the current scope fold. |
| `fold-close` | `zc` | Fold the current scope. |
| `fold-open` | `zo` | Open the fold at the current line. |
| `fold-close-all` | `zM` | Fold all known LSP scopes. |
| `fold-open-all` | `zR` | Open all folds. |
| `jumps` | `jl` | Open the jump history list. |
| `jump-back` | `jb` | Jump to the previous recorded location. |
| `jump-forward` | `jf` | Jump to the next recorded location. |
| `toggle-raster-policy` | `rp` | Toggle text rasterization policy. |
| `global-search` | `gs` | Open workspace global search. |
| `config-open` | `co` | Open the active config file, preferring the local override before the global config. |
| `config-reload` | `cr` | Reload global and local config files and reapply session overrides. |
| `set` | `cfg` | Apply a session config override, for example `set editor.font-size=14`. |

## Git Sidebar

These keys apply while the Git sidebar has focus. `Enter`, `Right`, and `l`
toggle group rows and commit rows; on changed-file rows they open the diff in
the focused code pane.

| Key | Action |
| --- | --- |
| `Up`, `k` | Move to the previous visible Git row. |
| `Down`, `j` | Move to the next visible Git row. |
| `Left`, `h` | Collapse the selected Git group or commit row. |
| `Right`, `l`, `Enter` | Toggle the selected group or commit row, or open the selected file diff. |
| `g` | Move to the first visible Git row. |
| `G` | Move to the last visible Git row. |
| `K` | Show commit details for the selected graph commit. |
| `s` | Stage the selected unstaged or untracked file. |
| `u` | Unstage the selected staged file. |
| `r` | Refresh status and commit data. |
| `p` | Request `git push`. |
| `P` | Request `git pull --rebase --autostash`. |
| `Space` | Start the normal leader prefix, so sidebar focus still supports `Space g`, `Space e`, and the other leader commands. |

The Git sidebar also includes branch and commit search fields, explicit fetch,
pull, and push controls, and ref-based merge, rebase, and cherry-pick buttons.

## Git Diff View

Git diff buffers are read-only. Editing commands such as insert, delete,
change, paste, replace, indentation, and LSP actions are ignored.

| Key | Action |
| --- | --- |
| `Left`, `Right`, `h`, `l` | Move one character left or right. |
| `Up`, `Down`, `k`, `j` | Move one visible line up or down. |
| `Home`, `0` | Move to the start of the current diff row. |
| `End`, `$` | Move to the end of the current diff row. |
| `Ctrl+Left`, `Ctrl+Right`, `w`, `b`, `e` | Move by normal word boundaries. |
| `W`, `B`, `E` | Move by big-word boundaries. |
| `Ctrl+Home`, `g g` | Move to the start of the diff. |
| `Ctrl+End`, `G` | Move to the last diff row. Number prefixes such as `42 G` are not used in diff view. |
| `z z` | Center the cursor row in the focused split. |
| `v`, `V` | Toggle character-wise or line-wise visual selection. Motions extend the selection. |
| `y` | Copy the active selection. |
| `,` | Clear selection and extra cursors. |
| `/` | Open current-diff search. |
| `n`, `N` | Jump to the next or previous search match. |
| `:` | Open the command line. |
| `Space` | Start the normal leader prefix. |
| `u` | Toggle between inline and side-by-side diff text layouts. |

## Filesystem Sidebar

These keys apply while the filesystem sidebar has focus.

| Key | Action |
| --- | --- |
| `Tab`, `Shift+Tab` | Cycle to the next or previous visible tree entry. |
| `Up`, `k` | Move to the previous visible tree entry. |
| `Down`, `j` | Move to the next visible tree entry. |
| `Left`, `h` | Collapse the current directory. If it is already collapsed, move to the parent. At the root, close the tree. |
| `Right`, `l` | Expand the current directory, open the root, or open the selected file. |
| `Enter` | Toggle the root or directory open state, or open the selected file. |
| `<` | Collapse the current directory without moving to its parent. |
| `>` | Expand the current directory. |
| `g g` | Move the tree cursor to the root. |
| `G` | Move the tree cursor to the last visible entry. |
| `i`, `I` | Rename the current tree entry, placing the cursor at the beginning of the name. |
| `a`, `A` | Rename the current tree entry, placing the cursor at the end of the name. |
| `o` | Create a new file under the current tree location. |
| `O` | Create a new directory under the current tree location. |
| `d d` | Queue deletion of the current tree entry. |
| `u` | Undo a queued or applied filesystem tree operation. |
| `U` | Redo a filesystem tree operation. |
| `Ctrl+Z` | Undo a filesystem tree operation. |
| `Ctrl+Shift+Z` | Redo a filesystem tree operation. |
| `Ctrl+D` | Scroll the filesystem panel down by half a page. |
| `Ctrl+U` | Scroll the filesystem panel up by half a page. |
| `Space` | Start the normal leader prefix, so sidebar focus still supports `Space e`, `Space f`, and the other leader commands. |

## Filesystem Create And Rename Edits

These keys apply while a create or rename text edit is active in the filesystem
sidebar.

| Key | Action |
| --- | --- |
| Printable text | Insert text into the edited file or directory name. |
| `Enter` | Commit the create or rename operation. |
| `Esc` | Cancel the create or rename operation. |
| `Left`, `Right`, `Home`, `End` | Move within the edited name. |
| `Shift+Left`, `Shift+Right`, `Shift+Home`, `Shift+End` | Extend selection within the edited name. |
| `Ctrl+Left`, `Ctrl+Right` | Move by word within the edited name. |
| `Backspace` | Delete the previous character or selection. |
| `Ctrl+Backspace` | Delete the previous word or selection. |
| `Delete` | Delete the next character or selection. |
| `Ctrl+Delete` | Delete the next word or selection. |
| `Ctrl+Z` | Undo the edit text change. |
| `Ctrl+Shift+Z` | Redo the edit text change. |

## Save And Conflict Dialogs

| Key | Action |
| --- | --- |
| `Enter` in save-path dialog | Save to the typed path. |
| `Ctrl+S` in save-path dialog | Save to the typed path. |
| `Esc` in save-path dialog | Cancel save-as and close the dialog. |
| `C` in unsaved-close popup | Close without saving changes. |
| `S` in unsaved-close popup | Save changes, then continue the close action. |
| `O` in external-edit popup | Overwrite the file on disk with the buffer contents. |
| `R` in external-edit popup | Reload the buffer from disk. |
| `Esc` in config-error popup | Close the visible config error popup. |

The file-deleted-on-disk dialog currently exposes buttons only: close the
buffer, or save it as a new file.
