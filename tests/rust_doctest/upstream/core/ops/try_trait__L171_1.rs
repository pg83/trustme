// Extracted from library/core/src/ops/try_trait.rs:171
#![allow(unused)]
#![feature(try_trait_v2)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::ops::Try;
        
        assert_eq!(<Result<_, String> as Try>::from_output(3), Ok(3));
        assert_eq!(<Option<_> as Try>::from_output(4), Some(4));
        assert_eq!(
            <std::ops::ControlFlow<String, _> as Try>::from_output(5),
            std::ops::ControlFlow::Continue(5),
        );
        
        fn make_question_mark_work() -> Option<()> {
        assert_eq!(Option::from_output(4)?, 4);
        None }
        make_question_mark_work();
        
        // This is used, for example, on the accumulator in `try_fold`:
        let r = std::iter::empty().try_fold(4, |_, ()| -> Option<_> { unreachable!() });
        assert_eq!(r, Some(4));
        Ok(())
    }
    doctest().unwrap();
}
