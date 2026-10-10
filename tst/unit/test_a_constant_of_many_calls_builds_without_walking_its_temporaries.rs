// language-tags (utoipa's dependencies) has `pub const LANGUAGES:
// [LanguageSubtag; 8212] = [LanguageSubtag::new([..]), ..]`, and it took us
// 52 seconds. Every call in the constant's MIR got a cleanup path, and building
// one walked every value its scopes hold - thousands of temporaries of a
// `Copy` type - looking up each one's state with a linear search: cubic in the
// number of calls. Upstream schedules no drop at all for a value whose type
// has no drop glue (`schedule_drop`, rustc_mir_build/src/builder/scope.rs),
// so none of them is on any cleanup path; a walk over the scheduled drops now
// passes over such a value before asking anything about its state.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Subtag([u8; 3]);

impl Subtag {
    const fn new(subtag: [char; 3]) -> Self {
        Subtag([subtag[0] as u8, subtag[1] as u8, subtag[2] as u8])
    }
}

macro_rules! doubled {
    (0, [$($t:tt)*]) => { [$($t)*] };
    (1, [$($t:tt)*]) => { doubled!(0, [$($t)* $($t)*]) };
    (2, [$($t:tt)*]) => { doubled!(1, [$($t)* $($t)*]) };
    (3, [$($t:tt)*]) => { doubled!(2, [$($t)* $($t)*]) };
    (4, [$($t:tt)*]) => { doubled!(3, [$($t)* $($t)*]) };
    (5, [$($t:tt)*]) => { doubled!(4, [$($t)* $($t)*]) };
    (6, [$($t:tt)*]) => { doubled!(5, [$($t)* $($t)*]) };
    (7, [$($t:tt)*]) => { doubled!(6, [$($t)* $($t)*]) };
    (8, [$($t:tt)*]) => { doubled!(7, [$($t)* $($t)*]) };
    (9, [$($t:tt)*]) => { doubled!(8, [$($t)* $($t)*]) };
    (10, [$($t:tt)*]) => { doubled!(9, [$($t)* $($t)*]) };
    (11, [$($t:tt)*]) => { doubled!(10, [$($t)* $($t)*]) };
    (12, [$($t:tt)*]) => { doubled!(11, [$($t)* $($t)*]) };
    (13, [$($t:tt)*]) => { doubled!(12, [$($t)* $($t)*]) };
    (14, [$($t:tt)*]) => { doubled!(13, [$($t)* $($t)*]) };
}

pub const TABLE: [Subtag; 16384] = doubled!(14, [Subtag::new(['a', 'b', 'c']),]);

fn main() {
    assert_eq!(TABLE[16383], Subtag(*b"abc"));
}
