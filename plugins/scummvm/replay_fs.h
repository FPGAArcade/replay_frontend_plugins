// Replay Filesystem for ScummVM
//
// Custom filesystem implementation that integrates with Replay's VFS.
// Currently falls back to POSIX until VFS integration is fully implemented.

#ifndef REPLAY_FS_H
#define REPLAY_FS_H

#include "backends/fs/abstract-fs.h"
#include "backends/fs/fs-factory.h"
#include "common/str.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ReplayFilesystemNode - File/directory node backed by Replay VFS or native filesystem

class ReplayFilesystemNode : public AbstractFSNode {
public:
    // Create root node
    ReplayFilesystemNode();

    // Create node from path
    explicit ReplayFilesystemNode(const Common::String &path);

    virtual ~ReplayFilesystemNode() {}

    // AbstractFSNode interface
    virtual bool exists() const override;
    virtual Common::U32String getDisplayName() const override { return _displayName; }
    virtual Common::String getName() const override { return _displayName; }
    virtual Common::String getPath() const override { return _path; }
    virtual bool isDirectory() const override { return _isDirectory && _isReadable; }
    virtual bool isReadable() const override { return _isReadable; }
    virtual bool isWritable() const override { return _isWritable; }

    virtual AbstractFSNode *getChild(const Common::String &n) const override;
    virtual bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override;
    virtual AbstractFSNode *getParent() const override;

    virtual Common::SeekableReadStream *createReadStream() override;
    virtual Common::SeekableWriteStream *createWriteStream(bool atomic) override;
    virtual bool createDirectory() override;

protected:
    // Create a new node for the given path
    virtual AbstractFSNode *makeNode(const Common::String &path) const {
        return new ReplayFilesystemNode(path);
    }

    // Update flags from filesystem
    void setFlags();

    Common::String _displayName;
    Common::String _path;
    bool _isDirectory;
    bool _isValid;
    bool _isReadable;
    bool _isWritable;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ReplayFilesystemFactory - Creates ReplayFilesystemNode objects

class ReplayFilesystemFactory : public FilesystemFactory {
public:
    virtual AbstractFSNode *makeRootFileNode() const override;
    virtual AbstractFSNode *makeCurrentDirectoryFileNode() const override;
    virtual AbstractFSNode *makeFileNodePath(const Common::String &path) const override;
};

#endif // REPLAY_FS_H
