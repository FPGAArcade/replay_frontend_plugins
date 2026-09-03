// Replay Filesystem for ScummVM - Implementation
//
// Currently uses POSIX filesystem calls. Will be enhanced to use Replay VFS.

// Re-enable some forbidden symbols for filesystem operations
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h
#define FORBIDDEN_SYMBOL_EXCEPTION_unistd_h
#define FORBIDDEN_SYMBOL_EXCEPTION_mkdir
#define FORBIDDEN_SYMBOL_EXCEPTION_getenv

#include "replay_fs.h"
#include "backends/fs/stdiostream.h"
#include "common/algorithm.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cstdlib>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: Get home directory

static Common::String getHomeDirectory() {
    const char* home = getenv("HOME");
    if (home && *home) {
        return Common::String(home);
    }
    return Common::String(".");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ReplayFilesystemNode - Root constructor

ReplayFilesystemNode::ReplayFilesystemNode()
    : _isDirectory(true)
    , _isValid(true)
    , _isReadable(true)
    , _isWritable(false) {
    _path = "/";
    _displayName = _path;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ReplayFilesystemNode - Path constructor

ReplayFilesystemNode::ReplayFilesystemNode(const Common::String &p)
    : _isDirectory(false)
    , _isValid(false)
    , _isReadable(false)
    , _isWritable(false) {

    assert(p.size() > 0);

    // Expand "~/" to home directory
    if (p.hasPrefix("~/") || p.hasPrefix("~\\")) {
        Common::String homeDir = getHomeDirectory();
        _path = homeDir + (p.c_str() + 1);
    } else {
        _path = p;
    }

    // Normalize the path (remove unneeded slashes etc.)
    _path = Common::normalizePath(_path, '/');
    _displayName = Common::lastPathComponent(_path, '/');

    setFlags();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Update flags from filesystem

void ReplayFilesystemNode::setFlags() {
    struct stat st;

    if (stat(_path.c_str(), &st) == 0) {
        _isValid = true;
        _isDirectory = S_ISDIR(st.st_mode);
        _isReadable = (access(_path.c_str(), R_OK) == 0);
        _isWritable = (access(_path.c_str(), W_OK) == 0);
    } else {
        _isValid = false;
        _isDirectory = false;
        _isReadable = false;
        _isWritable = false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if path exists

bool ReplayFilesystemNode::exists() const {
    return _isValid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get child node

AbstractFSNode *ReplayFilesystemNode::getChild(const Common::String &n) const {
    assert(!_path.empty());
    assert(_isDirectory);

    // Make sure the string contains no slashes
    assert(!n.contains('/'));

    // Build the new path
    Common::String newPath(_path);
    if (_path.lastChar() != '/') {
        newPath += '/';
    }
    newPath += n;

    return makeNode(newPath);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get children (directory listing)

bool ReplayFilesystemNode::getChildren(AbstractFSList &myList, ListMode mode, bool hidden) const {
    assert(_isDirectory);

    DIR *dirp = opendir(_path.c_str());
    if (dirp == nullptr) {
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dirp)) != nullptr) {
        const char *d_name = entry->d_name;

        // Skip 'invisible' files if necessary
        if (d_name[0] == '.' && !hidden) {
            continue;
        }

        // Skip '.' and '..' to avoid cycles
        if ((d_name[0] == '.' && d_name[1] == '\0') ||
            (d_name[0] == '.' && d_name[1] == '.' && d_name[2] == '\0')) {
            continue;
        }

        // Build the full path
        Common::String childPath(_path);
        if (_path.lastChar() != '/') {
            childPath += '/';
        }
        childPath += d_name;

        // Create a new node for this child
        ReplayFilesystemNode *child = new ReplayFilesystemNode(childPath);

        // Skip files that are invalid
        if (!child->_isValid) {
            delete child;
            continue;
        }

        // Honor the chosen mode
        if ((mode == Common::FSNode::kListFilesOnly && child->_isDirectory) ||
            (mode == Common::FSNode::kListDirectoriesOnly && !child->_isDirectory)) {
            delete child;
            continue;
        }

        myList.push_back(child);
    }

    closedir(dirp);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get parent node

AbstractFSNode *ReplayFilesystemNode::getParent() const {
    if (_path == "/") {
        return nullptr;  // Root has no parent
    }

    const char *start = _path.c_str();
    const char *end = start + _path.size();

    // Strip off the last component
    while (end > start && *(end - 1) != '/') {
        end--;
    }

    if (end == start) {
        return nullptr;
    }

    // Don't include the trailing slash, except for root
    if (end - start > 1) {
        end--;
    }

    Common::String parentPath(start, end);
    AbstractFSNode *parent = makeNode(parentPath);

    if (!parent->isDirectory()) {
        delete parent;
        return nullptr;
    }

    return parent;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create read stream

Common::SeekableReadStream *ReplayFilesystemNode::createReadStream() {
    return StdioStream::makeFromPath(getPath(), StdioStream::WriteMode_Read);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create write stream

Common::SeekableWriteStream *ReplayFilesystemNode::createWriteStream(bool atomic) {
    return StdioStream::makeFromPath(getPath(),
        atomic ? StdioStream::WriteMode_WriteAtomic : StdioStream::WriteMode_Write);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create directory

bool ReplayFilesystemNode::createDirectory() {
    if (mkdir(_path.c_str(), 0755) == 0 || errno == EEXIST) {
        setFlags();
    }
    return _isValid && _isDirectory;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ReplayFilesystemFactory - Create root node

AbstractFSNode *ReplayFilesystemFactory::makeRootFileNode() const {
    return new ReplayFilesystemNode("/");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ReplayFilesystemFactory - Create current directory node

AbstractFSNode *ReplayFilesystemFactory::makeCurrentDirectoryFileNode() const {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) != nullptr) {
        return new ReplayFilesystemNode(buf);
    }
    return new ReplayFilesystemNode("/");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ReplayFilesystemFactory - Create node from path

AbstractFSNode *ReplayFilesystemFactory::makeFileNodePath(const Common::String &path) const {
    return new ReplayFilesystemNode(path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
