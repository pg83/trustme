//@ crate-type: lib

trait Project {
    type Output;
}

struct Source;

impl Project for Source {
    type Output = u32;
}

struct Holder {
    value: <Source as Project>::Output,
}

fn value() -> <Source as Project>::Output {
    0
}

fn projected<T>(holder: Holder) -> T
where
    Source: Project<Output = T>,
{
    let _: T = value();
    holder.value
}

// A preserved projection must be normalized before looking for an explicit
// trait impl.  In particular, auto-trait fallback must not destructure the
// projected type and ignore its explicit impl.
struct SyncSource;

struct ProjectedCell(std::cell::UnsafeCell<u32>);

unsafe impl Sync for ProjectedCell {}

impl Project for SyncSource {
    type Output = ProjectedCell;
}

struct SyncHolder {
    value: <SyncSource as Project>::Output,
}

fn assert_sync<T: Sync>() {}

fn projected_impl_is_visible() {
    assert_sync::<SyncHolder>();
}
