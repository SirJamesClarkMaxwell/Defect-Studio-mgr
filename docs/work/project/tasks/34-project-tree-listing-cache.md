# Task 34: the Project Tree re-reads the disk every frame

Reported 2026-09-18: the app becomes slow after mounting a folder from `K:` (a FUSE volume, ~11 TB).
Local folders feel fine. Diagnosed before this file was written; the line numbers below were read,
not guessed.

## Root cause

`ProjectTreePanel` lists directories from inside its draw function, so every expanded directory is
enumerated once per frame - and it happens **twice** per frame:

- `ProjectTreePanel.cpp:220` - `rebuildVisibleFlatList()` runs at the top of every draw and walks
  every expanded directory through `collectVisibleEntries` (`:455`), which calls
  `FileSystem::ListDirectory` (`:457`).
- `ProjectTreePanel.cpp:1046` - `renderDirectoryContents(section.path)` walks the same tree again
  while drawing it, recursing at `:1280`, calling `FileSystem::ListDirectory` at `:1139`.

`FileSystem::ListDirectory` (`Core/Utils/Path.cpp:53`) does a `directory_iterator` plus an
`entry.is_directory()` **per entry**. `renderRootSection` also calls `FileSystem::Exists` per root
per frame (`:1042`).

So each visible entry costs two `readdir` traversals and two `stat` calls every frame. On NTFS the
OS cache makes that invisible. On a FUSE mount each call crosses into userspace and probably the
network, and at 60 FPS with a hundred visible entries that is tens of thousands of filesystem round
trips per second. The tree is not slow because the folder is big; it is slow because nothing is
cached.

## Why it appeared only now - this is a regression, not a standing limitation

The user mounted this same folder before without any slowness. Confirmed cause:
**`881a529`, 2026-08-30, "feat(project-tree): file ops (add/copy/cut/paste/rename/delete/drag-drop)"**.

Before that commit `m_VisibleFlatList` was filled *during* the render walk - the removed
`m_VisibleFlatList.push_back(entryPath)` inside `renderDirectoryContents` - so the tree walked the
directory tree **once** per frame and the keyboard nav consumed the previous frame's list. That
commit replaced it with a separate `rebuildVisibleFlatList()` call that performs its own complete
walk, while the render walk stayed. One traversal per frame became two, and the rebuild is now
unconditional rather than gated on window focus.

Doubling a cost that is free on NTFS is still free; doubling it on a FUSE mount is what the user is
feeling. Collapsing the two walks back into one is therefore part of this fix, not an optimisation
on top of it - the cache is what makes that safe, since both walkers can then read the same listing.

Second, smaller regression from the same commit, worth fixing while in here because it shares the
symptom and not the cause: selection and Shift-anchor lookups do `std::find_if` over the whole
`m_VisibleFlatList` for **every drawn row** (`ProjectTreePanel.cpp:284-288` and the keyboard cursor
search at `:218-220`), which is quadratic in the number of visible entries. That touches no
filesystem at all, so it will not show up in the cache measurements - fix it with an index map or a
set lookup, and keep it a separate commit from the cache so the two can be judged apart.

## Goal

The Project Tree reads a directory from disk when something could have changed, not when a frame is
drawn. Mounting a slow volume stops costing frame rate.

## Design

One listing cache owned by the panel, consulted by **both** walkers so the double traversal
collapses to at most one real read:

```cpp
struct CachedDirectoryListing
{
    std::vector<DirectoryEntryInfo> entries;
    std::chrono::steady_clock::time_point readAt;
    bool exists = true; // folds in the per-root FileSystem::Exists call
};
std::unordered_map<std::string, CachedDirectoryListing> m_ListingCache;
```

with one accessor - `const CachedDirectoryListing &listingFor(const Path &)` - that returns the
cached entry, reading from disk only when there is no entry or it has aged past a TTL. Every current
`FileSystem::ListDirectory` and per-root `FileSystem::Exists` call in this panel goes through it.

**TTL.** Two seconds is the starting point: long enough that a slow mount is read at most once every
two seconds per visible directory instead of 60 times, short enough that a file created by an
external tool shows up without the user wondering whether the panel is broken. Put the number in one
named constant with a comment saying what it trades off.

**Explicit invalidation**, which is what makes the TTL acceptable:

- Any file operation the panel itself performs - create, rename, delete, paste, drag-move - drops the
  affected directories from the cache immediately, so the user's own actions are never delayed.
- Expanding a directory the cache has never seen reads it once, as now.
- The existing refresh affordance (or a new one if there is none) clears the whole cache.
- `IOLayer` file events, if any already reach this panel, drop the matching directory.

**Do not** add a filesystem watcher. On a FUSE mount a watcher is either unsupported or as expensive
as the polling it replaces, and it is a much larger change than this defect justifies.

## Files to create or change

- `src/Presentation/Panels/ProjectTreePanel.cpp` / `.hpp` - the cache, the accessor, and routing
  every existing call through it.
- `tests/` - see Acceptance criteria.

## Files that must NOT be touched

- `src/Core/Utils/Path.cpp` - `FileSystem::ListDirectory` is correct as it stands; it is called too
  often, which is the panel's fault, not its own. Other callers must keep the current behaviour.
- `src/Domain/`, `src/App/`, `premake5.lua`.
- The tree's selection, keyboard navigation and drag-and-drop behaviour. `rebuildVisibleFlatList`
  must keep producing exactly the same list it produces today - it is what Shift-click ranges and
  arrow-key navigation resolve against, and a stale or reordered list breaks both.

## Acceptance criteria

1. A GoogleTest over the cache (extract it so it is testable without ImGui - a small class taking a
   "list this directory" callback, not a direct `FileSystem` call) asserts that two lookups of the
   same directory inside the TTL perform exactly **one** underlying read.
2. A GoogleTest asserts an explicit invalidation forces the next lookup to read again.
3. A GoogleTest asserts a lookup after the TTL has elapsed reads again (inject the clock; do not
   sleep in a test).
4. A GoogleTest asserts a directory that does not exist is cached as `exists == false` and does not
   re-stat on every lookup.
5. Manual check, to be reported: with a folder from the slow mount expanded, the panel performs at
   most one read per directory per TTL window rather than one per frame. State how you verified it.
6. `scripts/Windows/Build.bat --config Release` builds both targets with zero errors and zero
   warnings. 2 skipped tests expected (`DS_PYTHON_CAPI_AVAILABLE=0`).

## Constraints

- Layer boundaries from `AGENTS.md` are hard. This is Presentation-local caching; it does not become
  an IO service and does not go through the EventBus.
- Only the main thread mutates state visible in the project or UI. Do not move the listing onto a
  thread in this task - caching removes the cost, threading would only hide it and introduce a
  second problem.
- `.cpp` files stay under ~500 lines. `ProjectTreePanel.cpp` is already well over that; the cache
  goes in its own file rather than being appended.
- Do not build or run anything needing an approval step. Report what you changed; the dispatching
  session builds, runs the suite and exercises the app.
