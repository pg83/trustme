//@ edition: 2021
//@ aux-build: cov99_w1317_inferred_item.rs

fn main() {
    let length = cov99_w1317_inferred_item::inferred_item();
    assert_eq!(length([1, 2, 3]), 3);
}
