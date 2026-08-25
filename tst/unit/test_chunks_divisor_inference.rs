// The IntoIterator bound determines the closure argument only after its body
// has been enumerated. The divisor must wait for that argument to make len()
// usize instead of independently falling back to i32.
fn accept_tree<S, B, I>(_root: S, _children_of: B)
where
    B: Fn(&S) -> I,
    I: IntoIterator<Item = S>,
{
}

fn main() {
    let nodes = (0..100).collect::<Vec<_>>();
    accept_tree(nodes.as_slice(), |&root| {
        root
            .split_last()
            .into_iter()
            .filter_map(|(_, rest)| if rest.is_empty() { None } else { Some(rest) })
            .flat_map(|rest| rest.chunks(rest.len() / 5))
    });
}
