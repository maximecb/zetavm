var math = import "std/math/0";
/// Utility functions for arrays

/// Returns the first index i, where arr[i] == value
/// if no such index found, returns -1
exports.indexOf = function (arr, value)
{
    var len = arr.length;
    for (var i = 0; i < len; i += 1)
    {
        if (arr[i] == value)
        {
            return i;
        }
    }
    return -1;
};

/// Maps function mapper on arr. Returns an array with
/// ith element as mapper[arr[i]]. arr remains unchanged.
exports.map = function(arr, mapper)
{
    var newArr = [];
    var len = arr.length;
    for (var i = 0; i < len; i += 1)
    {
        newArr:push(mapper(arr[i]));
    }
    return newArr;
};

/// Returns an array with elements of arr which
/// satisfy predicate. i.e. predicate(arr[i]) returns true.
exports.filter = function(arr, predicate)
{
    var newArr = [];
    var len = arr.length;
    for (var i = 0; i < len; i += 1)
    {
        if (predicate(arr[i]))
        {
            newArr:push(arr[i]);
        }
    }
    return newArr;
};

/// For each element in arr it calls consumer
/// It does not modify arr.
exports.forEach = function(arr, consumer)
{
    var len = arr.length;
    for (var i = 0; i < len; i += 1)
    {
        consumer(arr[i]);
    }
};

/// Returns subarray of arr with starting index
/// st and ending index end-1. i.e. (inclusive, exclusive)
exports.slice = function(arr, st, end)
{
    // Don't worry about array bounds vm/runtime will
    // take care of it.
    var newArr = [];
    for (var i = st; i < end; i += 1)
    {
        newArr:push(arr[i]);
    }
    return newArr;
};

/// Concatenates two arrays a and b into new array
/// and returns it. It does not modify either a or b.
exports.concat = function(a, b)
{
    var newArr = [];
    var lenA = a.length;
    var lenB = b.length;
    for (var i = 0; i < lenA; i += 1)
    {
        newArr:push(a[i]);
    }
    for (var i = 0; i < lenB; i += 1)
    {
        newArr:push(b[i]);
    }
    return newArr;
};

/// Appends array b to array a. It does not modify
/// array b.
exports.append = function(a, b)
{
    var len = b.length;
    for (var i = 0; i < len; i += 1)
    {
        a:push(b[i]);
    }
};

/// Replaces value at every index i with b
/// where arr[i] == a.
exports.replace = function(arr, a, b)
{
    var len = arr.length;
    for (var i = 0; i < len; i += 1)
    {
        if (arr[i] == a)
        {
            arr[i] = b;
        }
    }
};

/// Does the shallow equality check
exports.arrayEq = function(a, b)
{
    if (a.length != b.length)
    {
        return false;
    }
    for (var i = 0; i < a.length; i += 1)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
};

/// Checks if the array 'arr' contains the value
exports.contains = function(arr, value)
{
    return exports.indexOf(arr, value) != -1;
};

/// Searches through the array, using the binary search algorithm, for a given item.
/// returning the index of where the item is found within the array.
exports.binarySearch = function(arr, item)
{
    var left = 0;
    var right = arr.length - 1;

    for (;left <= right;)
    {
        var m = math.idiv(left + right, 2);
        var element = arr[m];

        if (element < item)
        {
            left = m + 1;
        }
        else if (element > item)
        {
            right = m - 1;
        }
        else
        {
            return m;
        }
    }

	// Item not found.
    return -1;
};

// Sorts the given array using a comparison function.
// The "compare" function takes two arguments, a and b
//  - Return greater than 0 if a is greater than b
//  - Return 0 if a equals b
//  - Return less than 0 if a is less than b
exports.sort = function(array, compFunc)
{
    quickSort(array, 0, array.length - 1, compFunc);
};

//============================================================================
// Internal functions below
//============================================================================

var quickSort = function(array, low, high, comp) {
    // Hoare partition scheme
    if (low < high) {
        var p = partition(array, low, high, comp);
        quickSort(array, low, p, comp);
        quickSort(array, p+1, high, comp);
    }
};

var partition = function(array, low, high, comp) {
    var pivot = array[low];
    var i = low - 1;
    var j = high + 1;
    
    for (;;) {
        // do while loop equivalent, quite messy.
        for (;;) {
            i += 1;
            if (!(comp(array[i], pivot) < 0)) { // array[i] < pivot
                break;
            }
        }

        for (;;) {
            j -= 1;
            if (!(comp(array[j], pivot) > 0)) { // array[j] > pivot
                break;
            }
        }
        
        if (i >= j) {
            return j;
        }

        swap(array, i, j);
    }
};

var swap = function(array, a, b) {
    var tmp = array[a];
    array[a] = array[b];
    array[b] = tmp;
};