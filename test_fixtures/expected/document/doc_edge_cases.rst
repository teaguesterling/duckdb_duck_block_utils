Edge Cases & Special Characters
===============================

Testing quotes: "double" and ``'single'`` and apostrophes: it's working!

Code Escaping
-------------

.. code:: python

   def example():
       """Docstring with "quotes" and 'apostrophes'"""
       return f"Value: {x} | Special: <>&"

.. code:: sql

   -- SQL with operators
   SELECT * FROM t WHERE x > 10 AND y < 20;
   SELECT 'string with ''quotes''';

Inline special chars: ``<html>``, ``a && b``, ``x | y``, ``$PATH``

URLs with Parameters
--------------------

Link with params: `Search Results <https://example.com/search?q=test&page=1>`__

Mathematical Expressions
------------------------

Comparisons: x < y, a > b, n <= m, p >= q

Operators: a + b - c * d / e % f

Unicode Support
---------------

International: cafe, naive, resume

Symbols: arrows, checks, stars

Emoji-adjacent: (tm), (c), (r)
