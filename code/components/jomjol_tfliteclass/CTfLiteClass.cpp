#include "CTfLiteClass.h"
#include "ClassLogFile.h"
#include "Helper.h"
#include "psram.h"
#include "esp_log.h"
#include "../../include/defines.h"

#include <sys/stat.h>
#include <new>
#include <limits.h>

// #define DEBUG_DETAIL_ON


static const char *TAG = "TFLITE";


void CTfLiteClass::MakeStaticResolver()
{
  resolver.AddFullyConnected();
  resolver.AddReshape();
  resolver.AddSoftmax();
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddQuantize();
  resolver.AddMul();
  resolver.AddAdd();
  resolver.AddLeakyRelu();
  resolver.AddDequantize();
}


float CTfLiteClass::GetOutputValue(int nr)
{
    if (this->interpreter == nullptr || !lastInvokeSucceeded || nr < 0) return -1000.0f;
    TfLiteTensor* output2 = this->interpreter->output(0);
    if (output2 == nullptr || output2->dims == nullptr || output2->dims->size < 2) return -1000.0f;

    int numeroutput = output2->dims->data[1];
    if (nr >= numeroutput || output2->data.f == nullptr)
      return -1000;

    return output2->data.f[nr];
}


int CTfLiteClass::GetClassFromImageBasis(CImageBasis *rs)
{
    if (interpreter == nullptr || !LoadInputImageBasis(rs))
      return -1000;

    Invoke();
    if (!lastInvokeSucceeded) return -1000;
    return GetOutClassification();
}


int CTfLiteClass::GetOutClassification(int _von, int _bis)
{
  if (interpreter == nullptr || !lastInvokeSucceeded) return -1;
  TfLiteTensor* output2 = interpreter->output(0);

  float zw_max;
  float zw;
  int zw_class;

  if (output2 == NULL || output2->dims == nullptr || output2->dims->size < 2 || output2->data.f == nullptr)
    return -1;

  int numeroutput = output2->dims->data[1];
  //ESP_LOGD(TAG, "number output neurons: %d", numeroutput);

  if (_bis == -1)
    _bis = numeroutput -1;

  if (_von == -1)
    _von = 0;

  if (_von < 0 || _bis < _von || _bis >= numeroutput)
  {
    ESP_LOGD(TAG, "NUMBER OF OUTPUT NEURONS does not match required classification!");
    return -1;
  }

  zw_max = output2->data.f[_von];
  zw_class = _von;
  for (int i = _von + 1; i <= _bis; ++i)
  {
    zw = output2->data.f[i];
    if (zw > zw_max)
    {
        zw_max = zw;
        zw_class = i;
    }
  }
  return (zw_class - _von);
}


void CTfLiteClass::GetInputDimension(bool silent)
{
  if (this->interpreter == nullptr) return;
  TfLiteTensor* input2 = this->interpreter->input(0);
  if (input2 == nullptr || input2->dims == nullptr) return;

  im_height = 0;
  im_width = 0;
  im_channel = 0;
  int numdim = input2->dims->size;
  if (!silent)  ESP_LOGD(TAG, "NumDimension: %d", numdim);

  int sizeofdim;
  for (int j = 0; j < numdim; ++j)
  {
    sizeofdim = input2->dims->data[j];
    if (!silent) ESP_LOGD(TAG, "SizeOfDimension %d: %d", j, sizeofdim);
    if (j == 1) im_height = sizeofdim;
    if (j == 2) im_width = sizeofdim;
    if (j == 3) im_channel = sizeofdim;
  }
}


int CTfLiteClass::ReadInputDimenstion(int _dim)
{
  if (_dim == 0)
    return im_width;
  if (_dim == 1)
    return im_height;
  if (_dim == 2)
    return im_channel;

  return -1;
}


int CTfLiteClass::GetAnzOutPut(bool silent)
{
  if (this->interpreter == nullptr) return 0;
  TfLiteTensor* output2 = this->interpreter->output(0);
  if (output2 == nullptr || output2->dims == nullptr) return 0;

  int numdim = output2->dims->size;
  if (numdim < 2 || output2->data.f == nullptr) return 0;
  if (!silent) ESP_LOGD(TAG, "NumDimension: %d", numdim);

  int sizeofdim;
  for (int j = 0; j < numdim; ++j)
  {
    sizeofdim = output2->dims->data[j];
    if (!silent) ESP_LOGD(TAG, "SizeOfDimension %d: %d", j, sizeofdim);
  }


  float fo;

  // Process the inference results.
  int numeroutput = output2->dims->data[1];
  for (int i = 0; i < numeroutput; ++i)
  {
   fo = output2->data.f[i];
    if (!silent) ESP_LOGD(TAG, "Result %d: %f", i, fo);
  }
  return numeroutput;
}


void CTfLiteClass::Invoke()
{
    lastInvokeSucceeded = false;
    if (interpreter == nullptr) return;
    if (interpreter->Invoke() == kTfLiteOk) lastInvokeSucceeded = true;
    else LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "TensorFlow Lite Invoke() failed");
}


bool CTfLiteClass::LoadInputImageBasis(CImageBasis *rs)
{
    if (rs == nullptr || !rs->ImageOkay() || interpreter == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "LoadInputImageBasis: image or interpreter is unavailable");
        return false;
    }

    TfLiteTensor* inputTensor = interpreter->input(0);
    if (inputTensor == nullptr || inputTensor->dims == nullptr ||
        inputTensor->data.f == nullptr || inputTensor->type != kTfLiteFloat32 ||
        inputTensor->dims->size < 4) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "LoadInputImageBasis: input tensor is unavailable or not float32 image data");
        return false;
    }

    const int imageWidth = rs->width;
    const int imageHeight = rs->height;
    if (imageWidth <= 0 || imageHeight <= 0 || rs->channels < 3) return false;

    const int expectedHeight = inputTensor->dims->data[1];
    const int expectedWidth = inputTensor->dims->data[2];
    const int expectedChannels = inputTensor->dims->data[3];
    if (expectedWidth != imageWidth || expectedHeight != imageHeight || expectedChannels != 3) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "LoadInputImageBasis: image dimensions do not match model input tensor");
        return false;
    }

    if (static_cast<size_t>(imageWidth) > SIZE_MAX / static_cast<size_t>(imageHeight)) return false;
    const size_t pixelCount = static_cast<size_t>(imageWidth) * static_cast<size_t>(imageHeight);
    if (pixelCount > SIZE_MAX / (3U * sizeof(float))) return false;
    const size_t requiredBytes = pixelCount * 3U * sizeof(float);
    if (inputTensor->bytes < requiredBytes) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "LoadInputImageBasis: model input tensor buffer is smaller than the image");
        return false;
    }

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CTfLiteClass::LoadInputImageBasis - Start");
#endif

    uint8_t* image = rs->RGBImageLock();
    if (image == nullptr) return false;
    float* inputData = inputTensor->data.f;
    size_t outputIndex = 0;
    for (int y = 0; y < imageHeight; ++y) {
        for (int x = 0; x < imageWidth; ++x) {
            const size_t sourceOffset =
                (static_cast<size_t>(y) * static_cast<size_t>(imageWidth) +
                 static_cast<size_t>(x)) * static_cast<size_t>(rs->channels);
            inputData[outputIndex++] = static_cast<float>(image[sourceOffset]);
            inputData[outputIndex++] = static_cast<float>(image[sourceOffset + 1]);
            inputData[outputIndex++] = static_cast<float>(image[sourceOffset + 2]);
        }
    }
    rs->RGBImageRelease();

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CTfLiteClass::LoadInputImageBasis - done");
#endif
    return true;
}


bool CTfLiteClass::MakeAllocate()
{
    if (!ownsSharedMemoryLease || tensor_arena == NULL || model == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "MakeAllocate: Tensor Arena lease or model is unavailable");
        return false;
    }
    MakeStaticResolver();

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CTLiteClass::Alloc start");
#endif

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CTfLiteClass::MakeAllocate");
    delete this->interpreter;
    this->interpreter = new (std::nothrow) tflite::MicroInterpreter(
        this->model, resolver, this->tensor_arena, this->kTensorArenaSize);
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Trying to load the model. If it crashes here, it is most likely due to a corrupted model!");

    if (this->interpreter == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "new tflite::MicroInterpreter failed");
        LogFile.WriteHeapInfo("CTfLiteClass::MakeAllocate-new tflite::MicroInterpreter failed");
        return false;
    }

    const TfLiteStatus allocate_status = this->interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "AllocateTensors() failed");
        delete this->interpreter;
        this->interpreter = nullptr;
        return false;
    }
    lastInvokeSucceeded = false;

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CTLiteClass::Alloc done");
#endif
    return true;
}


void CTfLiteClass::GetInputTensorSize()
{
#ifdef DEBUG_DETAIL_ON    
    float *zw = this->input;
    int test = sizeof(zw);
    ESP_LOGD(TAG, "Input Tensor Dimension: %d", test);
#endif
}


long CTfLiteClass::GetFileSize(std::string filename)
{
  struct stat stat_buf;
  long rc = -1;

  FILE *pFile = fopen(filename.c_str(), "rb"); // previously only "rb

  if (pFile != NULL)
  {
    rc = stat(filename.c_str(), &stat_buf);
    fclose(pFile);
  }
  
  return rc == 0 ? stat_buf.st_size : -1;
}


bool CTfLiteClass::ReadFileToModel(std::string _fn)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CTfLiteClass::ReadFileToModel: " + _fn);
    if (!ownsSharedMemoryLease || tensor_arena == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ReadFileToModel: Tensor Arena shared-memory lease was not acquired");
        return false;
    }

    const long size = GetFileSize(_fn);
    if (size <= 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Model file is missing or empty: " + _fn);
        return false;
    }
    if (static_cast<unsigned long>(size) > static_cast<unsigned long>(MAX_MODEL_SIZE)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Model file is larger than the reserved shared model region: " + _fn);
        return false;
    }

    FILE *pFile = fopen(_fn.c_str(), "rb");
    if (pFile == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ReadFileToModel: Could not open model file " + _fn);
        return false;
    }

    if (!modelMemoryClaimed) {
        modelfile = static_cast<unsigned char*>(psram_get_shared_model_memory());
        modelMemoryClaimed = (modelfile != NULL);
    }
    if (modelfile == NULL) {
        fclose(pFile);
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ReadFileToModel: Unable to claim shared model memory");
        LogFile.WriteHeapInfo("CTfLiteClass::ReadFileToModel");
        return false;
    }

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CTLiteClass::Alloc modelfile start");
#endif

    const size_t bytesRead = fread(modelfile, 1, static_cast<size_t>(size), pFile);
    const int closeResult = fclose(pFile);
    if (bytesRead != static_cast<size_t>(size) || closeResult != 0) {
        model = nullptr;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ReadFileToModel: Short read or close error while loading " + _fn);
        return false;
    }

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CTLiteClass::Alloc modelfile successful");
#endif
    return true;
}


bool CTfLiteClass::LoadModel(std::string _fn)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CTfLiteClass::LoadModel");

    if (!ReadFileToModel(_fn.c_str())) {
      return false;
    }

    model = tflite::GetModel(modelfile);

    if(model == nullptr)     
      return false;
    
    return true;
}


CTfLiteClass::CTfLiteClass()
{
    this->ownsSharedMemoryLease = false;
    this->modelMemoryClaimed = false;
    this->lastInvokeSucceeded = false;
    this->model = nullptr;
    this->modelfile = NULL;
    this->interpreter = nullptr;
    this->input = nullptr;
    this->output = nullptr;
    this->kTensorArenaSize = TENSOR_ARENA_SIZE;
    this->im_height = 0;
    this->im_width = 0;
    this->im_channel = 0;
    this->tensor_arena = static_cast<uint8_t*>(psram_get_shared_tensor_arena_memory());
    this->ownsSharedMemoryLease = (this->tensor_arena != NULL);
    if (!this->ownsSharedMemoryLease) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Unable to acquire shared Tensor Arena memory");
        LogFile.WriteHeapInfo("CTfLiteClass constructor");
    }
}


CTfLiteClass::~CTfLiteClass()
{
    delete this->interpreter;
    this->interpreter = nullptr;
    if (ownsSharedMemoryLease) {
        psram_free_shared_tensor_arena_and_model_memory();
        ownsSharedMemoryLease = false;
        modelMemoryClaimed = false;
        lastInvokeSucceeded = false;
        tensor_arena = nullptr;
        modelfile = nullptr;
    }
}        
