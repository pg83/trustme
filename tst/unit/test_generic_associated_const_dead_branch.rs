struct Bomb<T>(T);

impl<T> Bomb<T> {
    const VALUE: () = panic!();
}

const fn trigger<T>() {
    Bomb::<T>::VALUE
}

struct Guard<T, const RUN: bool>(T);

impl<T, const RUN: bool> Guard<T, RUN> {
    const VALUE: () = if RUN {
        trigger::<T>()
    };

    const FUNCTION: fn() = if RUN {
        || trigger::<T>()
    } else {
        || {}
    };
}

fn main() {
    let _ = Guard::<(), false>::VALUE;
    Guard::<(), false>::FUNCTION();
}
