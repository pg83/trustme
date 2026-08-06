// Extracted from library/alloc/src/rc.rs:160
use std::rc::Rc;
use std::rc::Weak;
use std::cell::RefCell;

struct Owner {
    name: String,
    gadgets: RefCell<Vec<Weak<Gadget>>>,
    // ...other fields
}

struct Gadget {
    id: i32,
    owner: Rc<Owner>,
    // ...other fields
}

fn main() {
    // Create a reference-counted `Owner`. Note that we've put the `Owner`'s
    // vector of `Gadget`s inside a `RefCell` so that we can mutate it through
    // a shared reference.
    let gadget_owner: Rc<Owner> = Rc::new(
        Owner {
            name: "Gadget Man".to_string(),
            gadgets: RefCell::new(vec![]),
        }
    );

    // Create `Gadget`s belonging to `gadget_owner`, as before.
    let gadget1 = Rc::new(
        Gadget {
            id: 1,
            owner: Rc::clone(&gadget_owner),
        }
    );
    let gadget2 = Rc::new(
        Gadget {
            id: 2,
            owner: Rc::clone(&gadget_owner),
        }
    );

    // Add the `Gadget`s to their `Owner`.
    {
        let mut gadgets = gadget_owner.gadgets.borrow_mut();
        gadgets.push(Rc::downgrade(&gadget1));
        gadgets.push(Rc::downgrade(&gadget2));

        // `RefCell` dynamic borrow ends here.
    }

    // Iterate over our `Gadget`s, printing their details out.
    for gadget_weak in gadget_owner.gadgets.borrow().iter() {

        // `gadget_weak` is a `Weak<Gadget>`. Since `Weak` pointers can't
        // guarantee the allocation still exists, we need to call
        // `upgrade`, which returns an `Option<Rc<Gadget>>`.
        //
        // In this case we know the allocation still exists, so we simply
        // `unwrap` the `Option`. In a more complicated program, you might
        // need graceful error handling for a `None` result.

        let gadget = gadget_weak.upgrade().unwrap();
        println!("Gadget {} owned by {}", gadget.id, gadget.owner.name);
    }

    // At the end of the function, `gadget_owner`, `gadget1`, and `gadget2`
    // are destroyed. There are now no strong (`Rc`) pointers to the
    // gadgets, so they are destroyed. This zeroes the reference count on
    // Gadget Man, so he gets destroyed as well.
}
