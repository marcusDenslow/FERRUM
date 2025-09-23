# FERRUM

FERRUM is a feature-rich, extensible shell written in C, designed for Linux systems. It offers advanced productivity enhancements, built-in commands, fuzzy search integration, git repository support, and interactive utilities for developers and power users.

## Features

- **Built-in Shell Commands**  
  Includes common shell commands (`cd`, `ls`, `dir`, `pwd`, `cat`, `history`, `copy`, `move`, `mkdir`, `rmdir`, `del`, `touch`, `clear`, `ps`, etc.) and many custom commands.
- **Powerful Directory Bookmarks**  
  Save, list, and quickly navigate to frequently used directories with `bookmark`, `bookmarks`, `goto`, and `unbookmark`.
- **Alias System**  
  Define command aliases for faster workflows.
- **Interactive Fuzzy Finder**  
  Integrated [fzf](https://github.com/junegunn/fzf) support for file, history, and command search.
- **Ripgrep Integration**  
  Perform lightning-fast code search using [ripgrep (rg)](https://github.com/BurntSushi/ripgrep) with interactive fuzzy filtering.
- **Tab Completion and Suggestions**  
  Tab completion for commands and file paths; type a partial command followed by `?` for suggestions.
- **Command History**  
  Persistent command history with navigation via arrow keys.
- **Focus Timer and Countdown Utility**  
  Pomodoro-style `focus_timer` and countdown timers to help manage your work intervals.
- **Git Integration**  
  Status bar displays repository info; built-in commands (`git_status`, `gg`) for quick access to common git actions.
- **Status Bar**  
  Dynamic status bar at the bottom of the terminal, including git info and timer display.
- **Themes**  
  Customizable shell color themes.
- **Diff Viewer**  
  Ncurses-powered diff viewer for git changes.
- **Weather and City Info**  
  Fetch weather or favorite cities’ info directly from the shell.
- **Extensible Builtins**  
  Easily add new built-in commands in C.

## Installation

### Prerequisites

- GCC (or compatible C compiler)
- ncurses library (`libncurses-dev` or equivalent)
- [fzf](https://github.com/junegunn/fzf) (optional, for fuzzy finding)
- [ripgrep](https://github.com/BurntSushi/ripgrep) (optional, for fast code search)

### Build Instructions

Clone the repository and build:

```sh
git clone https://github.com/marcusDenslow/FERRUM.git
cd FERRUM
make
```

This will produce the shell executable: `shell`

### Dependencies

- **fzf**: For interactive fuzzy finding.
  - Install with:  
    `git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf && ~/.fzf/install`
- **ripgrep**: For fast code search.
  - Install with:  
    `sudo apt install ripgrep`

## Usage

Start the shell:

```sh
./shell
```

### Common Built-in Commands

- `cd <dir>`: Change directory
- `ls` or `dir`: List files in current directory
- `pwd`: Print working directory
- `cat <file>`: View file contents
- `history`: Show command history
- `bookmark <name>`: Bookmark current directory
- `bookmarks`: List all bookmarks
- `goto <name>`: Jump to bookmarked directory
- `alias <name> <command>`: Create a command alias
- `grep <pattern>`: Search files with grep
- `ripgrep <pattern>`: Search files with ripgrep
- `fzf`: Launch fuzzy finder
- `git_status`: Show git repo status
- `gg <command>`: Quick git commands (`s`=status, `c`=commit, `p`=pull, etc.)
- `focus_timer <minutes>`: Start a focus timer
- `theme <name>`: Change shell theme

Type `help` or `help <command>` for detailed information on any command.

### Advanced Features

- **Tab completion:** Use Tab to auto-complete commands and paths.
- **Command suggestions:** Type a partial command + `?` for suggestions.
- **Status bar:** Shows useful info at the bottom of your terminal.

## Extending FERRUM

- Add new built-in commands in `builtins.c` and `builtins.h`.
- Extend fuzzy and ripgrep functionality in `fzf_native.c`, `ripgrep.c`.

## Contribution

Contributions, suggestions, and bug reports are welcome!

1. Fork the repository.
2. Create a new feature branch.
3. Make your changes.
4. Submit a pull request.

Please follow the style and structure of existing code.


**FERRUM**: Your productivity shell for the modern terminal.  
[GitHub Repository](https://github.com/marcusDenslow/FERRUM)
