import importlib
import sys

import iixml_matrix


def verify():
    namespace = {}
    exec("def adjustment(value):\n    return value + 7\n", namespace)
    matrix = iixml_matrix.Matrix((2, 2, 2), range(1, 9))
    product = matrix.tensordot(matrix, (1, 2), (1, 2))
    xml_roundtrip = iixml_matrix.Matrix.from_xml(product.to_xml())
    try:
        iixml_matrix.Matrix.from_xml("<matrix shape='1'><value>false</value></matrix>")
        invalid_rejected = False
    except iixml_matrix.MatrixError:
        invalid_rejected = True
    return {
        "implementation": sys.implementation.name,
        "matrix_compiled": hasattr(iixml_matrix, "__compiled__"),
        "matrix_origin": str(iixml_matrix.__spec__.origin),
        "matrix_loader": type(iixml_matrix.__spec__.loader).__name__,
        "probe_compiled": "__compiled__" in globals(),
        "values": product.values,
        "means": matrix.mean(axis=(0, 2)).values,
        "batches": (matrix @ matrix).values,
        "xml_roundtrip": xml_roundtrip.values,
        "invalid_rejected": invalid_rejected,
        "dynamic": namespace["adjustment"](5),
        "dynamic_import": importlib.import_module("math").sqrt(81),
    }
