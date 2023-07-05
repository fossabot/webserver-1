#pragma once
#include "HWCodecs/HWCodecsDeclarations.h"

constexpr int DECODER_SHARING_FACTOR = 10;

HWDecoderFactorySP CreateQSSharedDecoderHolder(QSDevice& device);
