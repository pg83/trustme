/* regex-syntax's `Drop for Ast`: `stack.push(mem::replace(&mut x.ast, empty_ast()))` with
   `x.ast: Box<Ast>` and `stack: Vec<Ast>`.  `push` is resolved only after its argument
   was enumerated; upstream resolves the method first and `replace` then takes its
   inputs from the expected `Ast` (`&mut Box<Ast>` dereferences to `&mut Ast`), so `T`
   is `Ast` - not `Box<Ast>` read off the first argument. */
use std::mem;

enum Ast {
    Empty,
    Group(Box<Group>),
}

struct Group {
    ast: Box<Ast>,
}

impl Ast {
    fn empty() -> Ast {
        Ast::Empty
    }
}

fn count(ast: &mut Ast) -> usize {
    let empty_ast = || Ast::empty();
    let mut stack = vec![mem::replace(ast, empty_ast())];
    let mut n = 0;
    while let Some(mut ast) = stack.pop() {
        n += 1;
        match ast {
            Ast::Empty => {}
            Ast::Group(ref mut x) => {
                stack.push(mem::replace(&mut x.ast, empty_ast()));
            }
        }
    }
    n
}

fn main() {
    let mut ast = Ast::Group(Box::new(Group { ast: Box::new(Ast::Group(Box::new(Group { ast: Box::new(Ast::Empty) }))) }));
    assert_eq!(count(&mut ast), 3);
}
