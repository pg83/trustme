// A trait method used as a function item decays to a signature whose return
// type is still the projection `Self::Output`. Coercing it to a function
// pointer has to normalize that projection: otherwise the pointer relation
// defers forever and the coercion is finally forced through as a plain
// equality, which a function item can never satisfy.

fn main() {
    let single: fn(i32, i32) -> i32 = std::ops::Add::add;
    assert_eq!(single(2, 3), 5);

    let chosen = match "-" {
        "+" => std::ops::Add::add,
        "-" => std::ops::Sub::sub,
        _ => unimplemented!(),
    };
    assert_eq!(chosen(5, 3), 2);

    let with_closure = (match "<" {
        "+" => std::ops::Add::add,
        "-" => std::ops::Sub::sub,
        "<" => |a, b| (a < b) as i32,
        _ => unimplemented!(),
    })(5, 5);
    assert_eq!(with_closure, 0);
}
