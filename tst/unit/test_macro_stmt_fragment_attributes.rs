macro_rules! with_statement {
    ({ $statement:stmt }) => {{
        $statement
    }};
}

fn main() {
    with_statement!({
        #[allow(unused_variables)]
        let value = 1
    });
}
