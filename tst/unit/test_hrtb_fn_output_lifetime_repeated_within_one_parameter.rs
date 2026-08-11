fn accepts<F>(function: F)
where
    F: for<'a> Fn((&'a u8, &'a u8)) -> &u8,
{
    let _ = function;
}

fn main() {}
