//@ crate-type: lib
// A projection equality carries the projection's trait bound to the equal
// method parameter when that parameter is used in a bounded type.

trait Project {
    type Assoc;
}

struct Needs<T: Project>(T);

fn accepted<T, U>() -> Option<Needs<U>>
where
    T: Project<Assoc = U>,
    T::Assoc: Project,
{
    None
}
