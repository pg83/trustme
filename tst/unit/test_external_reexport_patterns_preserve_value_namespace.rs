//@ aux-build: external_pattern_namespaces.rs
//@ compile-variant: -O

fn main() {
    let flag = external_pattern_namespaces::Flag;
    let checked = match flag {
        external_pattern_namespaces::nested::VisibleFlag => 101u16,
    };
    let constant = match external_pattern_namespaces::selected() {
        external_pattern_namespaces::nested::VISIBLE_SELECTED => checked,
        _ => 0,
    };
    assert_eq!(constant, 101);
}
