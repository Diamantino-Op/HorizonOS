#include "RBTree.hpp"

void RBTree::insert(const u64 data, const u64 extraData, u64 *extraArgs, const CreateNodeFun createNode) {
	auto *node = createNode(data, extraData, extraArgs);

	RBTreeNode *parent = nullptr;
	RBTreeNode *curr = this->root;

	while (curr != nullptr) {
		parent = curr;

		if (node->data < curr->data) {
			curr = curr->left;
		} else {
			curr = curr->right;
		}
	}

	node->parent = parent;

	if (parent == nullptr) {
		this->root = node;
	} else if (node->data < parent->data) {
		parent->left = node;
	} else {
		parent->right = node;
	}

	this->fixInsert(node);
}

void RBTree::remove(const u64 data, u64 *extraArgs, const DeleteNodeFun deleteNode) {
	RBTreeNode *node = this->findNode(this->root, data);

	if (node == nullptr) {
		return;
	}

	RBTreeNode *tmpNode = node;
	RBTreeColor tmpOriginalColor = tmpNode->color;

	RBTreeNode *child = nullptr;

	if (node->left == nullptr) {
		child = node->right;

		this->transplant(node, node->right);
	} else if (node->right == nullptr) {
		child = node->left;

		this->transplant(node, node->left);
	} else {
		tmpNode = this->minNode(node->right);

		tmpOriginalColor = tmpNode->color;
		child = tmpNode->right;

		if (tmpNode->parent == node) {
			child->parent = tmpNode;
		} else {
			this->transplant(tmpNode, tmpNode->right);
			tmpNode->right = node->right;
			tmpNode->right->parent = tmpNode;
		}

		this->transplant(node, tmpNode);

		tmpNode->left = node->left;
		tmpNode->left->parent = tmpNode;
		tmpNode->color = node->color;
	}

	deleteNode(tmpNode, extraArgs);

	if (tmpOriginalColor == RBTreeColor::BLACK) {
		this->fixDelete(child);
	}
}

RBTreeNode *RBTree::find(const u64 data) {
	return this->findNode(this->root, data);
}

RBTreeNode *RBTree::min() {
	return this->minNode(this->root);
}

RBTreeNode *RBTree::defaultCreateNode(const u64 data, const u64 extraData, u64 *) {
	return new RBTreeNode { .data = data, .extraData = extraData };
}

void defaultDeleteNode(const RBTreeNode *node, u64 *) {
	delete node;
}

void RBTree::rotateLeft(RBTreeNode *node) {
	RBTreeNode *right = node->right;
	node->right = right->left;

	if (right->left != nullptr) {
		right->left->parent = node;
	}

	right->parent = node->parent;

	if (node->parent == nullptr) {
		this->root = right;
	} else if (node == node->parent->left) {
		node->parent->left = right;
	} else {
		node->parent->right = right;
	}

	right->left = node;
	node->parent = right;
}

void RBTree::rotateRight(RBTreeNode *node) {
	RBTreeNode *left = node->left;
	node->left = left->right;

	if (left->right != nullptr) {
		left->right->parent = node;
	}

	left->parent = node->parent;

	if (node->parent == nullptr) {
		this->root = left;
	} else if (node == node->parent->right) {
		node->parent->right = left;
	} else {
		node->parent->left = left;
	}

	left->right = node;
	node->parent = left;
}

void RBTree::fixInsert(RBTreeNode *node) {
	while (node->parent != nullptr and node->parent->parent != nullptr and node->parent->color == RBTreeColor::RED) {
		if (node->parent == node->parent->parent->left) {
			RBTreeNode *uncle = node->parent->parent->right;

			if (uncle->color == RBTreeColor::RED) {
				node->parent->color = RBTreeColor::BLACK;
				uncle->color = RBTreeColor::BLACK;
				node->parent->parent->color = RBTreeColor::RED;

				node = node->parent->parent;
			} else {
				if (node == node->parent->right) {
					node = node->parent;

					this->rotateLeft(node);
				}

				node->parent->color = RBTreeColor::BLACK;
				node->parent->parent->color = RBTreeColor::RED;

				this->rotateRight(node->parent->parent);
			}
		} else {
			RBTreeNode *uncle = node->parent->parent->left;

			if (uncle != nullptr and uncle->color == RBTreeColor::RED) {
				node->parent->color = RBTreeColor::BLACK;
				uncle->color = RBTreeColor::BLACK;
				node->parent->parent->color = RBTreeColor::RED;

				node = node->parent->parent;
			} else {
				if (node == node->parent->left) {
					node = node->parent;

					this->rotateRight(node);
				}

				node->parent->color = RBTreeColor::BLACK;
				node->parent->parent->color = RBTreeColor::RED;

				this->rotateLeft(node->parent->parent);
			}
		}

		this->root->color = RBTreeColor::BLACK;
	}
}

void RBTree::fixDelete(RBTreeNode *node) {
	while (node != this->root and node->color == RBTreeColor::BLACK) {
		if (node == node->parent->left) {
			RBTreeNode *sibling = node->parent->right;

			if (sibling->color == RBTreeColor::RED) {
				sibling->color = RBTreeColor::BLACK;
				node->parent->color = RBTreeColor::RED;

				this->rotateLeft(node->parent);

				sibling = node->parent->right;
			}

			if (sibling->left->color == RBTreeColor::BLACK and sibling->right->color == RBTreeColor::BLACK) {
				sibling->color = RBTreeColor::RED;
				node = node->parent;
			} else {
				if (sibling->right->color == RBTreeColor::BLACK) {
					sibling->left->color = RBTreeColor::BLACK;
					sibling->color = RBTreeColor::RED;

					this->rotateRight(sibling);

					sibling = node->parent->right;
				}

				sibling->color = node->parent->color;
				node->parent->color = RBTreeColor::BLACK;
				sibling->right->color = RBTreeColor::BLACK;

				this->rotateLeft(node->parent);

				node = this->root;
			}
		} else {
			RBTreeNode *sibling = node->parent->left;

			if (sibling->color == RBTreeColor::RED) {
				sibling->color = RBTreeColor::BLACK;
				node->parent->color = RBTreeColor::RED;

				this->rotateRight(node->parent);

				sibling = node->parent->left;
			}

			if (sibling->right->color == RBTreeColor::BLACK and sibling->left->color == RBTreeColor::BLACK) {
				sibling->color = RBTreeColor::RED;
				node = node->parent;
			} else {
				if (sibling->left->color == RBTreeColor::BLACK) {
					sibling->right->color = RBTreeColor::BLACK;
					sibling->color = RBTreeColor::RED;

					this->rotateLeft(sibling);

					sibling = node->parent->left;
				}

				sibling->color = node->parent->color;
				node->parent->color = RBTreeColor::BLACK;
				sibling->left->color = RBTreeColor::BLACK;

				this->rotateRight(node->parent);

				node = this->root;
			}
		}
	}

	node->color = RBTreeColor::BLACK;
}

RBTreeNode *RBTree::findNode(RBTreeNode *node, const u64 data) {
	if (node == nullptr or data == node->data) {
		return node;
	}

	if (data < node->data) {
		return this->findNode(node->left, data);
	}

	return this->findNode(node->right, data);
}

RBTreeNode *RBTree::minNode(RBTreeNode *node) {
	while (node->left != nullptr) {
		node = node->left;
	}

	return node;
}

void RBTree::transplant(const RBTreeNode *node1, RBTreeNode *node2) {
	if (node1->parent == nullptr) {
		this->root = node2;
	} else if (node1 == node1->parent->left) {
		node1->parent->left = node2;
	} else {
		node1->parent->right = node2;
	}

	node2->parent = node1->parent;
}