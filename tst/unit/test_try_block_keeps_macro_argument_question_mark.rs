//@ edition: 2024
//@ test-harness

#![feature(future_join, try_blocks)]

mod outer {
    #[allow(unused_imports)]
    use std::future::{Future, join};

    mod inner {
        use super::*;

        #[test]
        fn question_mark_leaves_the_try_block() {
            let maybe_future = None;
            if false {
                *&mut { maybe_future } = Some(async {});
                loop {}
            }

            let result: Option<_> = try { join!(maybe_future?, async { unreachable!() }) };
            assert!(result.is_none());
        }

        #[test]
        fn question_mark_inside_closure_stays_inside_closure() {
            let result: Option<Option<()>> = try { (|| -> Option<()> { None? })() };
            assert_eq!(result, Some(None));
        }
    }
}
