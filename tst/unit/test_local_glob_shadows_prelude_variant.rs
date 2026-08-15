//@ edition: 2024
//@ test-harness

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Progress {
    None,
    Some,
    Complete,
}

use core::fmt::Debug as ParentDebug;

#[cfg(test)]
mod tests {
    use super::*;
    use Progress::*;

    fn require_parent_import<T: ParentDebug>() {}

    #[test]
    fn local_glob_wins() {
        require_parent_import::<Progress>();
        let states = [Complete, Some, None];
        assert!(matches!(states, [Progress::Complete, Progress::Some, Progress::None]));
    }
}
