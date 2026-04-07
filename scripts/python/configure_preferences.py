from __future__ import annotations

import argparse
import json
import sys

from scripts.python.common.preferences import load_preferences, save_preferences


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Configure user defaults for build and script flows."
    )
    parser.add_argument("--generator", choices=["vs2022", "gmake2"], default=None)
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default=None)
    parser.add_argument("--config", choices=["Debug", "Release", "Dist"], default=None)
    parser.add_argument("--target", default=None)
    parser.add_argument("--open", choices=["vs", "vscode", "none"], default=None)
    parser.add_argument("--update-repo", action="store_true", default=None)
    parser.add_argument("--python-group", action="append", default=None)
    parser.add_argument("--show", action="store_true")
    return parser


def prompt_for_preferences() -> int:
    """Interactive mode: ask user for preferences."""
    preferences = load_preferences()
    build_defaults = preferences.get("build", {})
    setup_defaults = preferences.get("setup", {})

    print("\n== Configure Preferences ==")
    print("(Press Enter to skip, or type value to set)\n")

    generator = input(
        f"Generator [vs2022/gmake2] (current: {build_defaults.get('generator', 'vs2022')}): "
    ).strip()
    if generator:
        build_defaults["generator"] = generator
        setup_defaults["generator"] = generator

    compiler = input(
        f"Compiler [msvc/gcc/clang] (current: {build_defaults.get('compiler', 'msvc')}): "
    ).strip()
    if compiler:
        build_defaults["compiler"] = compiler
        setup_defaults["compiler"] = compiler

    config = input(
        f"Config [Debug/Release/Dist] (current: {build_defaults.get('config', 'Debug')}): "
    ).strip()
    if config:
        build_defaults["config"] = config
        setup_defaults["config"] = config

    open_mode = input(
        f"Open IDE [vs/vscode/none] (current: {setup_defaults.get('open', 'none')}): "
    ).strip()
    if open_mode:
        setup_defaults["open"] = open_mode

    update_repo = (
        input(
            f"Auto-update repo? [true/false] (current: {setup_defaults.get('update_repo', False)}): "
        )
        .strip()
        .lower()
    )
    if update_repo in ("true", "false"):
        setup_defaults["update_repo"] = update_repo == "true"

    preferences["build"] = build_defaults
    preferences["setup"] = setup_defaults
    save_preferences(preferences)

    print("\nPreferences saved:")
    print(json.dumps(preferences, indent=2, sort_keys=True))
    return 0


def run(args: argparse.Namespace) -> int:
    preferences = load_preferences()

    if args.show:
        print(json.dumps(preferences, indent=2, sort_keys=True))
        return 0

    # If no arguments, run interactive mode
    has_args = (
        args.generator is not None
        or args.compiler is not None
        or args.config is not None
        or args.target is not None
        or args.open is not None
        or args.update_repo is not None
        or args.python_group is not None
    )
    if not has_args:
        return prompt_for_preferences()

    build_defaults = preferences.get("build", {})
    setup_defaults = preferences.get("setup", {})

    if args.generator is not None:
        build_defaults["generator"] = args.generator
        setup_defaults["generator"] = args.generator
    if args.compiler is not None:
        build_defaults["compiler"] = args.compiler
        setup_defaults["compiler"] = args.compiler
    if args.config is not None:
        build_defaults["config"] = args.config
        setup_defaults["config"] = args.config
    if args.target is not None:
        build_defaults["target"] = args.target
    if args.open is not None:
        setup_defaults["open"] = args.open
    if args.update_repo is not None:
        setup_defaults["update_repo"] = bool(args.update_repo)
    if args.python_group is not None:
        setup_defaults["python_groups"] = args.python_group

    preferences["build"] = build_defaults
    preferences["setup"] = setup_defaults
    save_preferences(preferences)
    print(json.dumps(preferences, indent=2, sort_keys=True))
    return 0


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
