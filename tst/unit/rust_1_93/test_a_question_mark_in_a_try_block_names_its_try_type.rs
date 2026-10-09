#![feature(try_blocks)]
// rustc 1.93 lowers a `?` inside a `try` block to
// `break 'catch Residual::into_try_type(residual)` (lang item
// `into_try_type`), so the block's type follows from the residual and the
// block's output instead of from its context; rustc 1.92 called
// `FromResidual::from_residual` and needed the type spelled out. We kept the
// 1.92 lowering and could not infer these blocks.

fn main() {
    let parsed = try {
        let x: i32 = "5".parse()?;
        x + 1
    };
    assert_eq!(parsed, Ok::<i32, std::num::ParseIntError>(6));
    let missing = try {
        let v: Option<u8> = None;
        v? + 1
    };
    assert_eq!(missing, None::<u8>);
}
