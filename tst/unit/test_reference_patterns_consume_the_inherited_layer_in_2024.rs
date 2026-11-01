//@ edition: 2024
#![feature(ref_pat_eat_one_layer_2024)]

fn main() {
    let values = [&7u16, &11u16];
    let [&first, second] = &values;
    let _: &u16 = first;
    let _: &&u16 = second;
    assert_eq!(*first, 7);
    assert_eq!(**second, 11);
    let [&head, tail @ ..] = &values;
    assert_eq!(*head, 7);
    assert_eq!(tail.len(), 1);
    assert_eq!(*tail[0], 11);
}
