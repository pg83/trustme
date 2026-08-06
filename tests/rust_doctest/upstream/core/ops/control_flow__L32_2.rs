// Extracted from library/core/src/ops/control_flow.rs:32
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::ops::ControlFlow;
        
        pub struct TreeNode<T> {
            value: T,
            left: Option<Box<TreeNode<T>>>,
            right: Option<Box<TreeNode<T>>>,
        }
        
        impl<T> TreeNode<T> {
            pub fn traverse_inorder<B>(&self, f: &mut impl FnMut(&T) -> ControlFlow<B>) -> ControlFlow<B> {
                if let Some(left) = &self.left {
                    left.traverse_inorder(f)?;
                }
                f(&self.value)?;
                if let Some(right) = &self.right {
                    right.traverse_inorder(f)?;
                }
                ControlFlow::Continue(())
            }
            fn leaf(value: T) -> Option<Box<TreeNode<T>>> {
                Some(Box::new(Self { value, left: None, right: None }))
            }
        }
        
        let node = TreeNode {
            value: 0,
            left: TreeNode::leaf(1),
            right: Some(Box::new(TreeNode {
                value: -1,
                left: TreeNode::leaf(5),
                right: TreeNode::leaf(2),
            }))
        };
        let mut sum = 0;
        
        let res = node.traverse_inorder(&mut |val| {
            if *val < 0 {
                ControlFlow::Break(*val)
            } else {
                sum += *val;
                ControlFlow::Continue(())
            }
        });
        assert_eq!(res, ControlFlow::Break(-1));
        assert_eq!(sum, 6);
        Ok(())
    }
    doctest().unwrap();
}
