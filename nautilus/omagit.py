# Nautilus extension: "Open in Omagit" for folders (and files) inside a git repository.
# Built into Omagit, whose Settings copy it to ~/.local/share/nautilus-python/extensions/omagit.py;
# Nautilus loads it when it starts (nautilus -q quits a running one).
import os
import shutil

from gi import require_version

require_version("Nautilus", "4.1")

from gi.repository import GObject, Gio, Nautilus  # noqa: E402

MAX_PARENT_LEVELS = 40


def _find_omagit():
    found = shutil.which("omagit")
    if found:
        return found
    for candidate in (
        os.path.expanduser("~/.local/bin/omagit"),
        "/usr/local/bin/omagit",
        "/usr/bin/omagit",
    ):
        if os.access(candidate, os.X_OK):
            return candidate
    return None


def _git_root(path):
    # Cheap stat() walk (no subprocess) so the menu thread stays snappy.
    current = path
    for _ in range(MAX_PARENT_LEVELS):
        try:
            if os.path.exists(os.path.join(current, ".git")):
                return current
        except OSError:
            return None
        parent = os.path.dirname(current)
        if parent == current:
            return None
        current = parent
    return None


class OpenInOmagitAction(GObject.GObject, Nautilus.MenuProvider):
    def _launch(self, _menu, path):
        omagit = _find_omagit()
        if not omagit:
            return
        argv = []
        setsid = shutil.which("setsid")
        uwsm_app = shutil.which("uwsm-app")
        if setsid and uwsm_app:
            argv = [setsid, uwsm_app, "--"]
        elif setsid:
            argv = [setsid]
        argv += [omagit, path]
        Gio.Subprocess.new(argv, Gio.SubprocessFlags.NONE)

    def _single_path(self, files):
        if len(files) != 1:
            return None
        location = files[0].get_location()
        if not location:
            return None
        path = location.get_path()
        if not path:
            return None
        if not files[0].is_directory():
            path = os.path.dirname(path)
        return path

    def _item(self, name, path):
        item = Nautilus.MenuItem(name=name, label="Open in Omagit", icon="omagit")
        item.connect("activate", self._launch, path)
        return item

    def get_file_items(self, *args):
        files = args[0] if len(args) == 1 else args[1]
        path = self._single_path(files)
        if not path or not _git_root(path) or not _find_omagit():
            return []
        return [self._item("Omagit::selected", path)]

    def get_background_items(self, *args):
        folder = args[0] if len(args) == 1 else args[1]
        path = self._single_path([folder])
        if not path or not _git_root(path) or not _find_omagit():
            return []
        return [self._item("Omagit::background", path)]
