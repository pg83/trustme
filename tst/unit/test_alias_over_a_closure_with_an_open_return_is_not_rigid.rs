//@ run-pass
/* clap_builder `debug_asserts.rs:527`: `cmd.get_keymap().keys().filter_map(|x| if let
   KeyType::Position(n) = x { Some(*n) } else { None }).max().unwrap_or(0)`, `keys()` an `impl
   Iterator`.  `max` asks `Self::Item: Ord` of `<FilterMap<I, closure(&K) -> ?R> as Iterator>::Item`
   while the closure's return is still open; the alias holds that variable only in the
   closure's signature, which the type walk does not enter, so it was taken for a rigid
   projection with no `Ord` impl - "No applicable methods" - where upstream's goal is merely
   ambiguous until the return is known. */
enum KeyType {
    Position(usize),
    Long(String),
}

struct MKeyMap(Vec<KeyType>);

impl MKeyMap {
    fn keys(&self) -> impl Iterator<Item = &KeyType> {
        self.0.iter()
    }
}

fn highest(map: &MKeyMap) -> usize {
    map.keys()
        .filter_map(|x| {
            if let KeyType::Position(n) = x {
                Some(*n)
            } else {
                None
            }
        })
        .max()
        .unwrap_or(0)
}

fn main() {
    let map = MKeyMap(vec![KeyType::Position(2), KeyType::Long("l".to_owned()), KeyType::Position(5)]);
    assert_eq!(highest(&map), 5);
    let value = (0..2048).map(|x| if x < 1024 { Some(x) } else { None }).take_while(|x| x.is_some()).map(|x| x.unwrap()).max();
    assert_eq!(value, Some(1023));
}
