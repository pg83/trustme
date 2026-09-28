// icu_collections builds consts with zerovec's `zeroslice!`, which calls its
// `$convert:expr` - a path to a const fn - on each element of a const array.
// The call of that value goes through `Fn::call(&f, (x,))`, whose `&f` a const
// context promotes to a static: the first argument is then a constant. rustc
// calls a fn item directly, its value unused; the rewrite to a direct call here
// read that argument as a place it never looks at.
macro_rules! convert_all {
    ($convert:expr; [$($x:expr),+]) => {{
        const X: &[u32] = &[$($convert($x)),+];
        X
    }};
}

const fn double(v: u32) -> u32 {
    v * 2
}

struct Wrap(u32);

const DOUBLED: &[u32] = convert_all!(double; [1, 2, char::MAX as u32]);

fn main() {
    assert_eq!(DOUBLED, &[2, 4, 0x10FFFF * 2]);
    let wrapped: &[Wrap] = &[Wrap(1)];
    let to_wrap = Wrap;
    assert_eq!(to_wrap(3).0 + wrapped[0].0, 4);
}
