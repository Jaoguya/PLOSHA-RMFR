"""
Dataset loading and transformation for PLOSHA-RMFR evaluation.

Provides the DatasetTransformer class that reads the IoTSyn IIoT
Network CSV and produces scheme-specific data for PLOSHA-RMFR and
all 4 baselines (Ref22, Ref24, Ref37, Ref38).
"""

from src.dataset.dataset_transformer import DatasetTransformer

__all__ = ["DatasetTransformer"]
