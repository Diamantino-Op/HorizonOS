#ifndef LIB_HOS_BASE_VECTOR_HPP
#define LIB_HOS_BASE_VECTOR_HPP

// Implementation from https://github.com/Miguel-Deniz/Vector-Implementation/blob/master/Vector%20Implementation/Vector.h

struct outOfRange {};

template<class T> class Vector {
public:
	/* ----- Constructors ----- */

	// Default constructor
	Vector();

	explicit Vector(usize s);

	// Copy constructor
	Vector(const Vector& arg);

	// Copy Assignment
	Vector<T>& operator=(const Vector<T>& arg);

	// Destructor
	~Vector();

	/*----------------------------*/





	/* -------- ITERATORS --------*/

	class iterator;

	iterator begin();

	iterator begin() const;

	iterator end();

	iterator end() const;

	iterator cBegin() const;

	iterator cEnd() const;

	/*----------------------------*/





	/* -------- CAPACITY -------- */

	bool empty() const;

	// Returns size of allocated storage capacity
	usize capacity() const;

	// Requests a change in capacity
	// reserve() will never decrease the capacity.
	void reserve(usize newMalloc);

	// Changes the Vector's size.
	// If the newSize is smaller, the last elements will be lost.
	// Has a default value param for custom values when resizing.
	void resize(usize newSize, T val = T());

	// Returns the size of the Vector (number of elements).
	usize size() const;

	// Returns the maximum number of elements the Vector can hold
	usize maxSize() const;

	// Reduces capacity to fit the size
	void shrinkToFit();

	/*----------------------------*/





	/* -------- MODIFIERS --------*/

	// Removes all elements from the Vector
	// Capacity is not changed.
	void clear();

	// Inserts element at the back
	void pushBack(const T& d);

	// Removes the last element from the Vector
	void popBack();

	/*----------------------------*/





	/* ----- ELEMENT ACCESS ----- */

	// Access elements with bound checking
	T& at(usize n);

	// Access elements with bounds checking for constant Vectors.
	const T& at(usize n) const;

	// Access elements, no bounds checking
	T& operator[](usize i);

	// Access elements, no bounds checking
	const T& operator[](usize i) const;

	// Returns a reference to the first element
	T& front();

	// Returns a reference to the first element
	const T& front() const;

	// Returns a reference to the last element
	T& back();

	// Returns a reference to the last element
	const T& back() const;

	// Returns a pointer to the array used by Vector
	T* data();

	// Returns a pointer to the array used by Vector
	const T* data() const;

	/*----------------------------*/

private:
	usize	_size;		// Number of elements in Vector
	T*		_elements;	// Pointer to the first element of Vector
	usize	_space;		// Total space used by Vector including
						// elements and free space.
};

template<class T> class Vector<T>::iterator {
public:
	iterator(T* p) :_curr(p) {}

	iterator& operator++() {
		++_curr;

		return *this;
	}

	iterator& operator--() {
		--_curr;

		return *this;
	}

	T& operator*() {
		return *_curr;
	}

	bool operator==(const iterator& b) const {
		return *_curr == *b._curr;
	}

	bool operator!=(const iterator& b) const {
		return *_curr != *b._curr;
	}

private:
	T* _curr;
};



// Constructors/Destructor
template<class T> Vector<T>::Vector() : _size(0), _elements(nullptr), _space(0) {}

template<class T> Vector<T>::Vector(const usize s) :_size(s), _elements(new T[s]), _space(s) {
	for (usize index = 0; index < _size; ++index) {
		_elements[index] = T();
	}
}

template<class T> Vector<T>::Vector(const Vector &arg) : _size(arg._size), _elements(new T[arg._size]), _space(arg._space) {
	for (usize index = 0; index < arg._size; ++index) {
		_elements[index] = arg._elements[index];
	}
}

template <class T> Vector<T>& Vector<T>::operator=(const Vector<T>& arg) {
	if (this == &arg) {
		return *this;	// Self-assignment not work needed
	}

	// Current Vector has enough space, so there is no need for new allocation
	if (arg._size <= _space) {
		for (usize index = 0; index < arg._size; ++index) {
			_elements[index] = arg._elements[index];
			_size = arg._size;

			return *this;
		}
	}

	T* p = new T[arg._size];

	for (int index = 0; index < arg._size; ++index) {
		p[index] = arg._elements[index];
	}

	delete[] _elements;
	_size = arg._size;
	_space = arg._size;
	_elements = p;

	return *this;
}

template<class T> Vector<T>::~Vector() {
	delete[] _elements;
}



// Iterators
template<class T> typename Vector<T>::iterator Vector<T>::begin() {
	return Vector<T>::iterator(&_elements[0]);
}

template<class T> typename Vector<T>::iterator Vector<T>::begin() const {
	return Vector<T>::iterator(&_elements[0]);
}

template<class T> typename Vector<T>::iterator Vector<T>::end() {
	return Vector<T>::iterator(&_elements[_size]);
}

template<class T> typename Vector<T>::iterator Vector<T>::end() const {
	return Vector<T>::iterator(&_elements[_size]);
}

template<class T> typename Vector<T>::iterator Vector<T>::cBegin() const {
	return Vector<T>::iterator(&_elements[0]);
}

template<class T> typename Vector<T>::iterator Vector<T>::cEnd() const {
	return Vector<T>::iterator(&_elements[_size]);
}



// Capacity
template<class T> bool Vector<T>::empty() const {
	return (_size == 0);
}

template<class T> usize Vector<T>::capacity() const {
	return _space;
}

template<class T> void Vector<T>::reserve(const usize newMalloc) {
	if (newMalloc <= _space) {
		return;
	}

	T* p = new T[newMalloc];

	for (usize i = 0; i < _size; ++i)
		p[i] = _elements[i];

	delete[] _elements;

	_elements = p;

	_space = newMalloc;
}

template<class T> void Vector<T>::resize(const usize newSize, T val) {
	reserve(newSize);

	for (usize index = _size; index < newSize; ++index)
		_elements[index] = T();

	_size = newSize;
}

template<class T> usize Vector<T>::size() const {
	return _size;
}



// Modifiers
template<class T> void Vector<T>::pushBack(const T& d) {
	if (_space == 0)
		reserve(8);
	else if (_size == _space)
		reserve(2 * _space);

	_elements[_size] = d;

	++_size;
}



// Accessors
template<class T> T & Vector<T>::at(usize n) {
	/*if (n < 0 || _size <= n) {
		throw outOfRange();
	}*/

	return _elements[n];
}

template<class T> const T & Vector<T>::at(usize n) const {
	/*if (n < 0 || _size <= n) {
		throw outOfRange();
	}*/

	return _elements[n];
}

template<class T> T & Vector<T>::operator[](usize i) {
	return _elements[i];
}

template<class T> const T & Vector<T>::operator[](usize i) const {
	return _elements[i];
}

template<class T> T& Vector<T>::front() {
	return _elements[0];
}

template<class T> const T& Vector<T>::front() const {
	return _elements[0];
}

template<class T> T& Vector<T>::back() {
	return _elements[_size - 1];
}

template<class T> const T& Vector<T>::back() const {
	return _elements[_size - 1];
}

template<class T> T* Vector<T>::data() {
	return _elements;
}

template<class T> const T* Vector<T>::data() const {
	return _elements;
}

#endif