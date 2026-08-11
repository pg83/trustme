macro_rules! nest_in_same_named_label {
    ($body:block) => {{
        'outer: loop {
            $body
            break 'outer;
        }
    }};
}

fn main() {
    let mut inner_value = 0;
    nest_in_same_named_label!({
        inner_value = 3;
    });
    assert_eq!(inner_value, 3);

    let mut value = 0;
    'outer: loop {
        value = 1;
        nest_in_same_named_label!({
            break 'outer;
        });
        value = 2;
        break;
    }
    assert_eq!(value, 1);
}
