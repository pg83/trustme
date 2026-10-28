//@ test-harness
#![feature(generic_assert)]

macro_rules! fails {
    ($name:ident, $condition:expr) => {
        fails!($name, $condition, "Assertion failed:");
    };
    ($name:ident, $condition:expr, $expected:literal) => {
        #[test]
        #[should_panic(expected = $expected)]
        fn $name() { assert!($condition); }
    };
}

fails!(literal, loop { break false; });
fails!(labelled_block, loop { break ('inner: { break 'inner false; }); }, "break ('inner: { break 'inner false })");
fails!(labelled_loop, loop { break ('inner: loop { break 'inner false; }); }, "break ('inner: loop { break 'inner false })");
fails!(binary_and_cast, loop { break (1u8 as u32 == 2); });
fails!(call_object, loop { break (|| false)(); });
fails!(field, loop { break (false, 3).0; });
fails!(index, loop { break [false][0]; });
fails!(method, loop { break [false].contains(&true); });
fails!(range_method, loop { break (1..3).contains(&4); });
fails!(labelled_while, { loop { break ('inner: while false { break 'inner; }); } false });
fails!(labelled_for, { loop { break ('inner: for _ in 0..0 { break 'inner; }); } false });
fails!(assignment, { let mut value = 0; loop { break (value = 1); } value == 0 });
fails!(assignment_op, { let mut value = 0; loop { break (value += 1); } value == 0 });
