// Extracted from library/core/src/cell.rs:159
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    use std::cell::OnceCell;

    struct Graph {
        edges: Vec<(i32, i32)>,
        span_tree_cache: OnceCell<Vec<(i32, i32)>>
    }

    impl Graph {
        fn minimum_spanning_tree(&self) -> Vec<(i32, i32)> {
            self.span_tree_cache
                .get_or_init(|| self.calc_span_tree())
                .clone()
        }

        fn calc_span_tree(&self) -> Vec<(i32, i32)> {
            // Expensive computation goes here
            vec![]
        }
    }
}
