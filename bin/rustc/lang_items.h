#pragma once

class HIRCrate;
struct HIRSimplePath;

namespace stl {
    class ObjPool;
}

/// The crate's lang-item paths, resolved once. Wired on the WireBoard.
///
/// These are constant for a crate, but every trait resolver used to look all
/// of them up by name in its constructor — with a resolver built per function
/// (and per expression) in each pipeline phase that is tens of thousands of
/// map lookups per compilation. Resolve them once here instead.
class LangItems {
public:
    virtual const HIRSimplePath& copy() const = 0;
    virtual const HIRSimplePath& clone() const = 0;
    virtual const HIRSimplePath& drop() const = 0;
    virtual const HIRSimplePath& sized() const = 0;
    virtual const HIRSimplePath& unsize() const = 0;
    virtual const HIRSimplePath& fn() const = 0;
    virtual const HIRSimplePath& fnMut() const = 0;
    virtual const HIRSimplePath& fnOnce() const = 0;
    virtual const HIRSimplePath& asyncFn() const = 0;
    virtual const HIRSimplePath& asyncFnMut() const = 0;
    virtual const HIRSimplePath& asyncFnOnce() const = 0;
    virtual const HIRSimplePath& box() const = 0;
    virtual const HIRSimplePath& phantomData() const = 0;
    virtual const HIRSimplePath& generator() const = 0;
    virtual const HIRSimplePath& discriminantKind() const = 0;
    virtual const HIRSimplePath& pointee() const = 0;
    virtual const HIRSimplePath& dynMetadata() const = 0;
    virtual const HIRSimplePath& pointeeSized() const = 0;
    virtual const HIRSimplePath& metaSized() const = 0;
    virtual const HIRSimplePath& destruct() const = 0;
    virtual const HIRSimplePath& future() const = 0;

    static LangItems* create(stl::ObjPool& pool, const HIRCrate& crate);
};
