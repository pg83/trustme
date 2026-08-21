trait Outer<const N: i32> {
    type Assoc: Inner<Item<N> = ()>;
}

trait Inner {
    type Item<const N: i32>;
}

fn prove<const N: i32, T: Outer<N>>() {
    let _: <<T as Outer<N>>::Assoc as Inner>::Item<N> = ();
}

fn main() {}
