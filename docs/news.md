# Release notes for version 3.6 {#news}

[TOC]


## New features {#features}

### Input event timestamps {#input_event_timestamps}

GLFW now provides the time each input event occurred via the new @ref
glfwGetInputEventTime function.  The time has millisecond resolution, shares
its clock and zero point with @ref glfwGetTime and is monotonically
non-decreasing per window, including across window focus changes.

## Caveats {#caveats}

## Deprecations {#deprecations}

## Removals {#removals}

## New symbols {#new_symbols}

### New functions {#new_functions}

- @ref glfwGetInputEventTime

### New types {#new_types}

### New constants {#new_constants}

## Release notes for earlier versions {#news_archive}

- [Release notes for 3.5](https://www.glfw.org/docs/3.5/news.html)
- [Release notes for 3.4](https://www.glfw.org/docs/3.4/news.html)
- [Release notes for 3.3](https://www.glfw.org/docs/3.3/news.html)
- [Release notes for 3.2](https://www.glfw.org/docs/3.2/news.html)
- [Release notes for 3.1](https://www.glfw.org/docs/3.1/news.html)
- [Release notes for 3.0](https://www.glfw.org/docs/3.0/news.html)
