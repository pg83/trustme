// Extracted from library/alloc/src/fmt.rs:253
#![allow(unused)]
extern crate alloc;
fn main() {
    // Hello {arg 0 ("x")} is {arg 1 (0.01) with precision specified inline (5)}
    println!("Hello {0} is {1:.5}", "x", 0.01);

    // Hello {arg 1 ("x")} is {arg 2 (0.01) with precision specified in arg 0 (5)}
    println!("Hello {1} is {2:.0$}", 5, "x", 0.01);

    // Hello {arg 0 ("x")} is {arg 2 (0.01) with precision specified in arg 1 (5)}
    println!("Hello {0} is {2:.1$}", "x", 5, 0.01);

    // Hello {next arg -> arg 0 ("x")} is {second of next two args -> arg 2 (0.01) with precision
    //                          specified in first of next two args -> arg 1 (5)}
    println!("Hello {} is {:.*}",    "x", 5, 0.01);

    // Hello {arg 1 ("x")} is {arg 2 (0.01) with precision
    //                          specified in next arg -> arg 0 (5)}
    println!("Hello {1} is {2:.*}",  5, "x", 0.01);

    // Hello {next arg -> arg 0 ("x")} is {arg 2 (0.01) with precision
    //                          specified in next arg -> arg 1 (5)}
    println!("Hello {} is {2:.*}",   "x", 5, 0.01);

    // Hello {next arg -> arg 0 ("x")} is {arg "number" (0.01) with precision specified
    //                          in arg "prec" (5)}
    println!("Hello {} is {number:.prec$}", "x", prec = 5, number = 0.01);
}
