//@ proc-macro-aux-build: replace_derive.rs
// pin-project's `#[pin_project(project_replace)]` re-emits the struct with
// `#[derive(::pin_project::__private::__PinProjectInternalDerive)]`. The
// derive spans the parameter `__replacement` with the user's
// `project_replace` token and uses `__replacement` at `Span::call_site()`.
// rustc's call site of a derive is its path's span, so both names are the
// user's. For a derive named by an absolute path we used an empty context
// as the call site, and the use did not find the parameter.
use replace_derive::with_replace;

#[with_replace(project_replace)]
struct Struct;

fn main() {
    assert_eq!(Struct.replace(7), 7);
}
