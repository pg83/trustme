pub use std::alloc::Layout;
pub fn oom(layout: Layout) -> ! { std::alloc::handle_alloc_error(layout) }
