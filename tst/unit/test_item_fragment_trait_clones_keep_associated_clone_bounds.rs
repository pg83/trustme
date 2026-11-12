macro_rules! copies {
    ($definition:item) => {
        mod left { $definition }
        mod right { $definition }
    };
}

copies! {
    pub trait Source {
        type Item: Clone;
        fn item() -> Self::Item;
    }
}

struct Value;

impl left::Source for Value {
    type Item = String;
    fn item() -> String { String::from("left") }
}

impl right::Source for Value {
    type Item = Vec<u16>;
    fn item() -> Vec<u16> { vec![23, 41] }
}

fn left_pair<T: left::Source>() -> (T::Item, T::Item) {
    let original = T::item();
    (original.clone(), original)
}

fn right_pair<T: right::Source>() -> (T::Item, T::Item) {
    let original = T::item();
    (original.clone(), original)
}

fn main() {
    assert_eq!(left_pair::<Value>(), (String::from("left"), String::from("left")));
    assert_eq!(right_pair::<Value>(), (vec![23, 41], vec![23, 41]));
}
