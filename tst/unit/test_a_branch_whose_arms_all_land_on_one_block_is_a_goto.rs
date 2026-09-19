/* A `&[..]` literal is promoted to its own `const`, and the const's body ends
   by closing the scope that holds every element temporary.  Closing that scope
   over an enum-typed temporary emits a `switch` on the variant with one arm per
   variant - and when no variant has anything to drop, every arm lands on the
   same block.  A few hundred temporaries leave a chain of a few hundred such
   switches.

   `MIROptimiseBlockSimplify` could only walk that chain through `Goto`s, so one
   link per `MIROptimise` round was collapsed - by `MIROptimiseConstPropagate`,
   which knows the variant of the value the switch reads - and the outer loop ran
   out of its hundred rounds ("Too many MIR optimisation iterations") long before
   the chain did.

   rustc has no fixpoint over its MIR passes at all; convergence comes from the
   order the passes run in.  Its `CfgSimplifier`
   (rustc_mir_transform/src/simplify.rs) therefore reaches its own fixpoint
   internally: `simplify_branch` turns a branch whose successors are all one
   block into a `Goto` and returns the duplicate arms' share of that block's
   predecessor count, and the loop around it alternates with `merge_successor`
   until neither fires.  `MIROptimiseBlockSimplify` now does the same, so the
   whole chain goes in one pass. */

#[derive(Clone, Copy, PartialEq, Debug)]
enum Value {
    Generic(u64),
    I8(i8),
    U8(u8),
    I16(i16),
    U16(u16),
    I32(i32),
    U32(u32),
    I64(i64),
    U64(u64),
    F32(u32),
    F64(u64),
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum Error {
    Mismatch,
    DivisionByZero,
}

fn main() {
    let mut rows = 0u32;
    let mut errors = 0u32;
    for &(v1, v2, result) in &[
        (Value::Generic(0), Value::I16(1), Ok(Value::I64(0))),
        (Value::I8(1), Value::U16(2), Ok(Value::U64(1))),
        (Value::U8(2), Value::I32(3), Ok(Value::F32(2))),
        (Value::I16(3), Value::U32(4), Ok(Value::F64(3))),
        (Value::U16(4), Value::I64(5), Err(Error::Mismatch)),
        (Value::I32(5), Value::U64(6), Ok(Value::I8(5))),
        (Value::U32(6), Value::F32(7), Ok(Value::U8(6))),
        (Value::I64(7), Value::F64(8), Ok(Value::I16(0))),
        (Value::U64(8), Value::Generic(0), Ok(Value::U16(1))),
        (Value::F32(0), Value::I8(1), Err(Error::Mismatch)),
        (Value::F64(1), Value::U8(2), Ok(Value::U32(3))),
        (Value::Generic(2), Value::I16(3), Ok(Value::I64(4))),
        (Value::I8(3), Value::U16(4), Ok(Value::U64(5))),
        (Value::U8(4), Value::I32(5), Ok(Value::F32(6))),
        (Value::I16(5), Value::U32(6), Err(Error::Mismatch)),
        (Value::U16(6), Value::I64(7), Ok(Value::Generic(1))),
        (Value::I32(7), Value::U64(8), Ok(Value::I8(2))),
        (Value::U32(8), Value::F32(0), Ok(Value::U8(3))),
        (Value::I64(0), Value::F64(1), Ok(Value::I16(4))),
        (Value::U64(1), Value::Generic(2), Err(Error::Mismatch)),
        (Value::F32(2), Value::I8(3), Ok(Value::I32(6))),
        (Value::F64(3), Value::U8(4), Ok(Value::U32(0))),
        (Value::Generic(4), Value::I16(5), Ok(Value::I64(1))),
        (Value::I8(5), Value::U16(6), Ok(Value::U64(2))),
        (Value::U8(6), Value::I32(7), Err(Error::Mismatch)),
        (Value::I16(7), Value::U32(8), Ok(Value::F64(4))),
        (Value::U16(8), Value::I64(0), Ok(Value::Generic(5))),
        (Value::I32(0), Value::U64(1), Ok(Value::I8(6))),
        (Value::U32(1), Value::F32(2), Ok(Value::U8(0))),
        (Value::I64(2), Value::F64(3), Err(Error::Mismatch)),
        (Value::U64(3), Value::Generic(4), Ok(Value::U16(2))),
        (Value::F32(4), Value::I8(5), Ok(Value::I32(3))),
        (Value::F64(5), Value::U8(6), Ok(Value::U32(4))),
        (Value::Generic(6), Value::I16(7), Ok(Value::I64(5))),
        (Value::I8(7), Value::U16(8), Err(Error::Mismatch)),
        (Value::U8(8), Value::I32(0), Ok(Value::F32(0))),
        (Value::I16(0), Value::U32(1), Ok(Value::F64(1))),
        (Value::U16(1), Value::I64(2), Ok(Value::Generic(2))),
        (Value::I32(2), Value::U64(3), Ok(Value::I8(3))),
        (Value::U32(3), Value::F32(4), Err(Error::Mismatch)),
    ] {
        rows += 1;
        if v1 == v2 {
            errors += 1;
        }
        let checked: Result<Value, Error> = result;
        if checked == Err(Error::DivisionByZero) {
            errors += 1;
        }
        if checked.is_err() {
            errors += 1;
        }
    }
    assert_eq!(rows, 40);
    assert_eq!(errors, 8);
}
