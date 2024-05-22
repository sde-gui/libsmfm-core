# libsmfm-core v1.2.0-alpha2, libsmfm-gtk2 v1.2.0-alpha2

libsmfm is a glib2-based library implementing core file manager facilities and UIs.

libsmfm was initially forked from libfm in 2013. libfm is the core library of pcmanfm file manager originally developed and maintained by PCMan, LStranger et al.

This is the first public release. The version numbering follows the original source code, so that this release is 1.2.0, as 1.1.0 was the latest libfm release before the fork made.

**Release Highlights:**

 * The source code is splitted into two git repositories, one for libsmfm-core and one for libsmfm-gtk. This is done to simplify tracking dependencies when packaging software.
   * https://github.com/sde-gui/libsmfm-core
   * https://github.com/sde-gui/libsmfm-gtk
 * Much attention is paid to performance improvements when navigating directories with enormous amount of files or having poor IO performance (HDD, FUSE, etc). In particular:
   * Internal implementation of FmFileInfo is reworked to allow concurent access from different threads whenever possible, and synchronization on mutexes added in various places as well.
   * IO operations, not strictly required right in the moment, are deferred in time and performed either asynchronously in a separate thread or on-demand. This affects stat() syscalls, MIME-type recognition and icon loading.
   * Some properties of FmFileInfo objects, which calculation may be a CPU-bound task, are also calculated on-demand.
   * Incremental multi-step update is implemented for folder model internal container as well as for folder view item layout. This reduces UI friezes and improves application response during file system search operations or opening folders containing tens or hundreds of thousands files.
   * Calculating the text label metrics may be a CPU-bound bootleneck on low-end or embedded hardware, or when dealing with hundreds of thousands of items - even on middle-end machines. Folder view relayout logic is now able to switch to the rough positioning of items, and then perform precise positioning in the background. This code path is activated automatically depending on the time spent in the item layout code.
   * Layout cache is implemented for FmCellRendererText items.
 * A number of improvements and fixes in File Properties dialog.
 * Various bugfixes in file search engine and UI.
 * Porting to GTK3 is no longer a goal. Being a part of SDE project, libsmfm primarily targets GTK2. GTK3 support code is still partially in the tree, but not even tested for building. It may be removed in the future versions. At the moment it is not entirely clear whether SDE will be ported to GTK3.


## libsmfm-core changes

**Performance improvements:**

 * Implementation of FmFileInfo significantly reworked, and parallelization of tasks added, as described in the section **Release Highlights** above.
 * FmFileInfo: Speed up `fm_file_info_is_directory()` for native paths.
 * FmFileInfo: Check for special folder path only when a path is actually a native directory.
 * FmDirListJob: Use POSIX APIs (not glib wrappers) to read directory entries for native paths.
 * FmMimeType: Implement fast content type guessing for executables.
 * FmMimeType: Add preallocated type objects for varoius `inode/*` types.

**Enhancements:**

 * FmNavHistory: Add option to delete duplicate items.
 * FmNavHistory: Add option remove_parent to make Recently Visited listing more compact.
 * FmPlacesModel: Add links to "My Computer" and the root "File System" to the places model.

**API changes:**

 * Add functions `fm_version_major()`, `fm_version_minor()`, `fm_version_micro()`, `fm_check_version()`.
 * Add function: `fm_get_mime_types_for_file_info_list()`.
 * FmNavHistory: `fm_nav_history_chdir()` now returns `FALSE` if the path is same as in the current history item; and `TRUE` otherwise. Previously this function was declared as returning void.
 * FmFileInfo: Add:
   * `fm_file_info_get_ctime()`
   * `fm_file_info_new_from_native_file()`
   * `fm_file_info_new_from_path_unfilled()`
   * `fm_file_info_is_directory()` as a synonym for `fm_file_info_is_dir()`
   * `fm_file_info_is_backup()`
 * FmFileInfo: Rename:
   * `fm_file_info_set_from_gfileinfo()` to `fm_file_info_fill_from_gfileinfo()`
   * `fm_file_info_set_from_native_file()` to `fm_file_info_fill_from_native_file()`
 * FmFileInfo: Delete unused enumeration FmFileInfoFlag.
 * FmFileInfo: Delete `fm_file_info_new_from_menu_cache_item()` and `fm_file_info_set_from_menu_cache_item()`.
 * Move `fm_file_info_list_*` functions from `fm-file-info.[hc]` to a separate set of files `fm-file-info-list.[hc]`.
 * Move `fm_path_list_*` functions from `fm-path.[hc]` to a separate set of files `fm-path-list.[hc]`.
 * FmList: Add functions `fm_list_push_head_uniq()` and `fm_list_push_tail_uniq()`.
 * FmList: Add field `item_compare` to `FmListFuncs`.
 * Add new class FmSymbol for immutable refcounted strings with optimized comparator.
 * FmIcon can now be loaded with FmThumbnailLoader.
 * Add support of reporting status for FmJob and FmFolder.
 * FmFolder: emit `content-changed` from the handler of job's `files-found` signal.
 * FmPath: Delete deprecated `fm_path_new_for_display_name()`.

**Bugfixes and stability improvements:**

 * FmMimeType: Fix handling broken links in `fm_mime_type_from_native_file()`.
 * Fix possible segfault in `fm_mime_type_unref()`, `fm_mime_type_ref()` by allowing mime_type argument being `NULL`.
 * FMPath: Remove unneeded `g_uri_escape_string()` in `fm_path_new_for_str()`.
 * Search engine: fix interrupt of recursive scanning when a folder matches search condition.
 * Search engine: do not match folders if search condition contains min_size or max_size.
 * Search engine: query G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE for matching of mime type.
 * Search engine: interpret backslash in mime type as slash.
 * Search engine: do not interrupt search on permission denied error.
 * `fm-thumbnail-loader.c`: Fix crash in `generate_thumbnails_with_builtin()`.
 * `fm-thumbnail-loader.c`: Don't use exif thumbnails when generating thumbnails of type LARGE since the size of embedded thumbnails may be insufficient. Keep reading image orintation from exif data.
 * `fm-thumbnail-loader.c`: Fix swapped orientations 6 (270 degrees) and 8 (90 degrees) for exif thumbnails.
 * `fm-file-launcher.c`: Fix handling of shortcuts in `fm_launch_files()`.
 * `fm-job.c`: protect modification of `thread_pool` with a lock.
 * `fm-job.c`: `fm_job_emit_error()`: abort a job without calling signal handler if `err == NULL`. Signal handlers expect `err` is always valid.
 * `fm-vfs-search.c`: Fix null pointer dereference on parsing malformed search URI.
 * Fix criticals about unset GIO attributes produced by GLib >= 2.77.

**Experimental features:**

 * Proof-of-concept implementation of file name highlighting.

**Changes in configuration options and variables:**

 * Add `libsmfm.conf` option `[places]places_computer`.
 * Add `libsmfm.conf` option `[places]places_root`.
 * Add `libsmfm.conf` option `[hacks]deferred_mime_type_loading`.
 * Add `libsmfm.conf` option `[hacks]exo_icon_view_pixbuf_hack`.
 * Add `libsmfm.conf` option `[hacks]exo_icon_draw_rectangle_around_selected_item`.
 * Add `libsmfm.conf` option `[ui]highlight_file_names`.
 * Add `libsmfm.conf` option `[ui]app_list_smart_grouping`.

**Building:**
 * Fix build with `-j`.
 * Fix out of tree build.
 * Fix build with recent GCC versions. (Missing `extern` specifier.)
 * Add support of automake 1.14.
 * Add support of automake 1.15.
 * Build with `--disable-static` by default.
 * glib2 version requirements increased from 2.22 to 2.32.
 * Delete `xml-purge.c`.
 * Add make targets `cppcheck` and `cppcheck-inconclusive` to run Cppcheck static analysis tool.

## libsmfm-gtk changes

**Performance improvements:**

 * Significant performance improvements in folder model handling and folder rendering logic, as described in the section **Release Highlights** above.
 * FmStandardView: Optimize invertion of selection.
 * Reduce flickering of folder view on model change by delaying expose event.

**Enhancements:**

 * File Properties Dialog:
   * Disable permission controls when the user is not file owner or root.
   * Display ctime.
   * Change labels for time fields to be in line with most other file managers:
     * ctime - "Created/Metadata Updated" (new)
     * mtime - "Modified" (previously: Last Modification)
     * atime - "Accessed" (previously: Last Access)
   * Add detailed tooltips for ctime, mtime, atime fields.
   * Improvements in displaying properties of multiple files:
     * Display common parent folder for multiple files.
     * Display ctime, mtime, atime intevals.
   * Do not display exact file size in the parentheses, if rough file size renders to the same value.
   * Display MIME type (in addition to human-readable description).
   * Make the dialog resizable.
   * If file name and display name differ, show both.
   * Display file icon and file name in the dialog title.
   * Allow editing associations for "inode/directory" from GUI. If `inode/directory` associated to a wrong application, various 3rd party applications can be affected (even struuman-desktop is affected), and there is absolutely no way to fix the things from GUI, since we have the appropriate controls disabled in the Properties dialog. So we do allow setting file associations for any mime types, including folders.
   * Fixed incorrect selection of default application in "Open with" combo box. `fm_app_chooser_combo_box_setup()` now explicitly calls `g_app_info_get_default_for_type()` and doesn't confuse "default" and "last used" application for a given mime type.
 * FmFolderView: Changed hotkey bindings:
   * `<Menu>` or `<Shift>F10` - open context menu for selected files.
   * `<Ctrl><Menu>` or `<Ctrl><F10>` - open context menu for the current folder.
 * FmFolderView: Modified `<Alt><Enter>` behavior. Now, when no files are selected, it opens the the current folder properties dialog.
 * FmStandardView: Hide icons when icon size is 0.
 * FmStandardView: Various improvements in item layout (fine-tuned padding etc).
 * FmStandardView: Always load thumbnails in Thumbnail View Mode.
 * FmPlacesView: Hide icons when icon size is 0.
 * Various code refactorings and rewriting a number of functions in more readable way.
 * Search Dialog: Set "Find" button as default in Search dialog.
 * Search Dialog: Allow items "Places to Search" to by reorderable by drag-n-drop.
 * More user friendly file delete confirmation dialog
   * Display the selected file name or number of multiple selected files in the dialog.
   * Display the message that permanent deletion cannot be undone.
 * FmFileMenu:
   * Display "Rename" in a separate section, not in the one where "Add Bookmark" and addtional menu items are located.
   * Applications listed in "Open With" menu can now be "smart grouped" when `[ui]app_list_smart_grouping` is enabled.

**Bugfixes and stability improvements:**

 * FmFolderView: `<Ctrl><Insert>` should be Copy and `<Shift><Insert>` should be Paste. They were switched places.
 * File Properties Dialog: Fix incorrect assumption in commit a7e371a1e90a0bc27264c10c5fe84021590d0855 "Don't show permissions tab if that info isn't available at all".
 * FmFolderView: Fix segfault when calling `get_custom_menu_callbacks()`.
 * Search Dialog: Fix "Smaller than" condition.
 * Search Dialog: Fix "Documents" condition.
 * Fix segfault in `on_mount_action_finished()`.
 * `fm_pixbuf_from_icon()`: fall back to gtk stock icons if themed icon loading failed:
   * `gtk-home` for `user-home`
   * `gtk-directory` for `folder` or `directory`
   * `gtk-file` for other icon names
 * Fix popup menu positioning on multimonitor configurations.

**API changes:**

 * `fm-file-menu.h`: Add function `fm_get_gtk_file_menu_for_string()`.
 * Add header `fm-app-utils.h` with function: `fm_app_utils_get_app_categories()`.
 * FmFolderView: Add functions:
   * `fm_folder_view_show_popup()`
   * `fm_folder_view_show_popup_for_selected_files()`
   * `fm_folder_view_get_popup_for_selected_files()`
 * FmFolderModel: Add functions:
   * `fm_folder_model_get_n_visible_items()`
   * `fm_folder_model_get_n_hidden_items()`
   * `fm_folder_model_get_n_incoming_items()`
 * FmFolderModel: Add signal `filtering-changed`.

**Experimental features:**

 * Implement pattern matching in folder-model and add GUI to input pattern in folder-view.

**Building:**

 * Fix out of tree build.
 * Add support of automake 1.14.
 * Add support of automake 1.15.
 * Build with `--disable-static` by default.
 * Build with `--disable-actions` by default.
 * Delete `xml-purge.c`.
 * Add make targets `cppcheck` and `cppcheck-inconclusive` to run Cppcheck static analysis tool.
 