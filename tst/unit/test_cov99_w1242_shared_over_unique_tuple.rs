//@ edition: 2021

fn main() {
    let mut inner = (37u32,);
    let outer = (&mut inner,);
    let ((shared,),) = &outer;
    let alias = shared;
    assert_eq!(*shared, 37);
    assert_eq!(*alias, 37);
}
