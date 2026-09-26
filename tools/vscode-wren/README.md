# Wren syntax highlighting for VS Code (local)

A minimal VS Code extension that adds syntax highlighting and basic editor
support for `.wren` files. It's meant as a stopgap until the official Wren
extension is ready, and is installed locally rather than from the Marketplace.

## Install

```sh
./tools/vscode-wren/build-vsix.sh
```

Then reload VS Code (Ctrl+Shift+P → **Developer: Reload Window**). Open any
`.wren` file and the language mode in the status bar should read **Wren**.

The script needs `zip` and the `code` CLI on your `PATH`. It doesn't need Node
or `vsce`: it zips the extension into `wren-local.vsix` by hand and runs
`code --install-extension` on it. The `.vsix` is a build artifact and is
git-ignored.

## What it highlights

- Comments: `//`, `/* */` (nested), and a leading `#!` shebang
- Strings: escapes, `%(...)` interpolation (contents highlighted as code), and
  `"""` raw strings
- Keywords, `true`/`false`/`null`, `this`/`super`
- Numbers (decimal, exponent, hex) and operators
- `import "module" for Name, Other`
- `class Foo is Bar` and `foreign class`
- Method, getter, setter, and `construct` definitions
- Calls like `Draw.clear(...)` and `foo(...)`
- Fields (`_field`) and static fields (`__field`)
- Capitalized names, highlighted as types
- Block parameters `{ |a, b| ... }` and `#attributes`

It also sets up comment toggling (Ctrl+/), bracket auto-closing, and basic
auto-indent.

## Files

| File | Purpose |
| --- | --- |
| `package.json` | Extension manifest; registers the `wren` language and grammar |
| `language-configuration.json` | Comments, brackets, auto-closing, indentation rules |
| `syntaxes/wren.tmLanguage.json` | TextMate grammar that does the highlighting |
| `build-vsix.sh` | Packs the folder into a `.vsix` and installs it |

## Making changes

1. Edit `syntaxes/wren.tmLanguage.json` (or the other files).
2. Rerun `./build-vsix.sh`. It reinstalls with `--force`.
3. Reload the VS Code window.

To see which grammar rule matched a piece of code, put the cursor on it and run
**Developer: Inspect Editor Tokens and Scopes**.

## Known limitations

- A method definition is only recognized when it starts a line. A one-liner
  placed after other code on the same line won't be treated as a definition.
- Every capitalized name is colored as a type, including module-level variables
  like `BgColor`. That matches how Wren treats them: capitalized names resolve
  at module scope.
- It provides highlighting only. There's no language server, so there's no
  autocomplete, go-to-definition, or error checking.

## Uninstall

```sh
code --uninstall-extension local.wren-local
```
