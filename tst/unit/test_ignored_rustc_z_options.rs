//@ compile-flags: -Z validate-mir -Zmir-enable-passes=+Inline -Zinline-mir=yes -Zunstable-options

fn main() {
    assert_eq!(6 * 7, 42);
}
