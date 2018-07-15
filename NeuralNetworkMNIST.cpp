// g++ -std=c++11 -stdlib=libc++ NeuralNetworkMNIST.cpp -o NeuralNetworkMNIST -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.13.sdk -Wl,-search_paths_first -Wl,-headerpad_max_install_names -framework OpenGL -framework OpenGL -framework GLUT -framework Cocoa -Wno-deprecated

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <pthread.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <float.h>
#include <mutex>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#define WIDTH 1280
#define HEIGHT 740

#define LEARNING_RATE 0.02
#define MOMENTUM 0.6
#define WEIGHT_DECAY 0.001

#define BMP_HEADER_SIZE 54

using namespace std;

struct Point2d {
        int x;
        int y;

        Point2d(int x, int y) {
                this->x = x;
                this->y = y;
        }

        Point2d(const Point2d& p) {
                this->x = p.x;
                this->y = p.y;
        }

        Point2d& operator = (const Point2d& p) {
                this->x = p.x;
                this->y = p.y;
                return *this;
        }

};

namespace NeuralNetwork {

class InputCase;
class Layer;

enum LayerType { convolutional, fc, relu, pool, dropout_layer };

//vector<Point2d> points;
int timebase_timestamp = 0;
int frame_counter = 0;
char current_fps_buffer[20];
float avg_error_percent = 100.0f;
char avg_error_percent_buffer[30];
unsigned char *producer_frame_buffer = NULL;
unsigned char *consumer_frame_buffer = NULL;
mutex consumer_mutex;
bool is_consuming_frame_buffer = false;

vector<Layer*> layers;

struct point_t
{
        int x, y, z;
};

struct size_t
{
        int width;
        int height;
        int depth;
};

class Gradient {

public:

float grad;
float oldgrad;
Gradient()
{
        grad = 0;
        oldgrad = 0;
}
};

struct range_t
{
        int min_x, min_y, min_z;
        int max_x, max_y, max_z;
};

static float update_weight(float w, Gradient* grad, float multp = 1)
{
        float m = (grad->grad + grad->oldgrad * MOMENTUM);
        w -= LEARNING_RATE * m * multp +
             LEARNING_RATE * WEIGHT_DECAY * w;
        return w;
}

static void update_gradient(Gradient* grad)
{
        grad->oldgrad = (grad->grad + grad->oldgrad * MOMENTUM);
}

/* BEGIN - VISUALS DATA STRUCTURES */
class TensorRenderFrameBuffer {

public:

int width; // pixels
int height; // pixels
int texture_width; // pixels
int texture_height; // pixels
char *caption = NULL;
GLuint texture = NULL;

unsigned char *producer_frame_buffer = NULL;
unsigned char *consumer_frame_buffer = NULL;
mutex consumer_mutex;
bool is_consuming_frame_buffer = false;

TensorRenderFrameBuffer(int _width, int _height) {
        texture_width = 64;
        texture_height = 64;
        width = _width;
        height = _height;

        producer_frame_buffer = (unsigned char *)malloc((texture_width * texture_height * 4));
        consumer_frame_buffer = (unsigned char *)malloc((texture_width * texture_height * 4));

        for(int x=0; x<texture_width; x++) {
                for(int y=0; y<texture_height; y++) {
                        producer_frame_buffer[(y * texture_width * 4) + x * 4] = 255;
                        producer_frame_buffer[(y * texture_width * 4) + x * 4 + 1] = 255; // green
                        producer_frame_buffer[(y * texture_width * 4) + x * 4 + 2] = 255;
                        producer_frame_buffer[(y * texture_width * 4) + x * 4 + 3] = 0; //Alpha

                        consumer_frame_buffer[(y * texture_width * 4) + x * 4] = 255;
                        consumer_frame_buffer[(y * texture_width * 4) + x * 4 + 1] = 255; // green
                        consumer_frame_buffer[(y * texture_width * 4) + x * 4 + 2] = 255;
                        consumer_frame_buffer[(y * texture_width * 4) + x * 4 + 3] = 0; //Alpha
                }
        }
}

void set(int x, int y, signed int value) const
{
        assert(x >= 0 && y >= 0);
        assert(x < texture_width && y < texture_height);
        producer_frame_buffer[(y * texture_width * 4) + x * 4] = (value < 0) ? (unsigned char)abs(value) : 0; // red
        producer_frame_buffer[(y * texture_width * 4) + x * 4 + 1] = (value >= 0) ? (unsigned char)value : 0; // green
        producer_frame_buffer[(y * texture_width * 4) + x * 4 + 2] = 0;
        producer_frame_buffer[(y * texture_width * 4) + x * 4 + 3] = 1.0f; //Alpha
}

void set128(int x, int y, int value) const
{
        assert(x >= 0 && y >= 0);
        assert(x < texture_width && y < texture_height);
        producer_frame_buffer[(y * texture_width * 4) + x * 4] = (value < 0) ? (unsigned char)(abs(value) * 2) : 0; // red
        producer_frame_buffer[(y * texture_width * 4) + x * 4 + 1] = (value >= 0) ? (unsigned char)(value * 2) : 0; // green
        producer_frame_buffer[(y * texture_width * 4) + x * 4 + 2] = 0;
        producer_frame_buffer[(y * texture_width * 4) + x * 4 + 3] = 1.0f; //Alpha
}

void swapBuffers()
{
        if(!is_consuming_frame_buffer) {
                consumer_mutex.lock();
                unsigned char *tmp = producer_frame_buffer;
                producer_frame_buffer = consumer_frame_buffer;
                consumer_frame_buffer = tmp;
                consumer_mutex.unlock();
        }
}

~TensorRenderFrameBuffer() {

        if(producer_frame_buffer != NULL) {
                delete[] producer_frame_buffer;
        }

        if(consumer_frame_buffer != NULL) {
                delete[] consumer_frame_buffer;
        }
}

};


class LayerGridFrameBuffer {

public:

int width;
int height;
char title[100] = "\0";
vector<char*> column_titles;
vector<char*> column_subtitles;
TensorRenderFrameBuffer **cells = NULL;

LayerGridFrameBuffer() {

}

LayerGridFrameBuffer(int _width, int _height, char *_title) {
        strcpy(title, _title);

        // Creates an array of pointers
        cells = new TensorRenderFrameBuffer*[_width * _height];

        // Initialize the array with null pointers
        for(int i=0; i<_width * _height; i++)
                cells[i] = NULL;
        width = _width;
        height = _height;
}

void setTitleForColumn(int col_index) {

}

TensorRenderFrameBuffer* operator()(int x, int y) const
{
        return this->get(x, y);
}

TensorRenderFrameBuffer* get(int x, int y) const
{
        assert(x >= 0 && y >= 0);
        assert(x < width && y < height);
        return cells[(y * width) + x];
}

void set(int x, int y, TensorRenderFrameBuffer* cell) const
{
        assert(x >= 0 && y >= 0);
        assert(x < width && y < height);
        cells[(y * width) + x] = cell;
}

~LayerGridFrameBuffer() {
        if(cells != NULL) {
                for(int i=0; i<width*height; i++)
                        if(cells[i] != NULL)
                                delete cells[i];
                delete[] cells;
        }

        for(int i=0; i<column_titles.size(); i++)
                delete column_titles[i];

        for(int i=0; i<column_subtitles.size(); i++)
                delete column_subtitles[i];

}

};
/* END - VISUALS DATA STRUCTURES */

class Tensor {

public:

size_t size;

};

class TensorFloat : public Tensor {

public:

float *values = NULL;

TensorFloat() {

}

TensorFloat(int width, int height, int depth) {
        values = new float[width * height * depth];
        size.width = width;
        size.height = height;
        size.depth = depth;
}

TensorFloat(const TensorFloat& t) {
        values = new float[t.size.width * t.size.height * t.size.depth];
        memcpy(this->values, t.values, t.size.width * t.size.height * t.size.depth * sizeof(float));
        this->size = t.size;
}

static TensorFloat* diff(TensorFloat *tensor_a, TensorFloat *tensor_b) {

        TensorFloat* clone = new TensorFloat(*tensor_a);
        for(int i = 0; i < tensor_b->size.width * tensor_b->size.height * tensor_b->size.depth; i++) {
                clone->values[i] -= tensor_b->values[i];
        }
        return clone;

}

float& operator()(int x, int y, int z) const
{
        return this->get( x, y, z );
}

float& get(int x, int y, int z) const
{
        assert(x >= 0 && y >= 0 && z >= 0);
        assert(x < size.width && y < size.height && z < size.depth);
        return values[z * (size.width * size.height) + y * size.width + x];
}

~TensorFloat() {
        if(values != NULL) {
                delete[] values;
        }
}

};

class TensorGradient : public Tensor {

public:

Gradient **values;

TensorGradient(int width, int height, int depth) {
        values = new Gradient*[width * height * depth];
        for(int i=0; i < width * height * depth; i++) {
                values[i] = new Gradient();
                values[i]->grad = 0;
                values[i]->oldgrad = 0;
        }
        size.width = width;
        size.height = height;
        size.depth = depth;
}

TensorGradient(const TensorGradient* t) {
        values = new Gradient*[t->size.width * t->size.height * t->size.depth];
        for(int i=0; i < t->size.width * t->size.height * t->size.depth; i++) {
                values[i] = new Gradient();
                values[i]->grad = t->values[i]->grad;
                values[i]->oldgrad = t->values[i]->oldgrad;
        }
        this->size = t->size;
}

Gradient* operator()(int x, int y, int z)
{
        return this->get(x, y, z);
}

Gradient* get(int x, int y, int z)
{
        assert(x >= 0 && y >= 0 && z >= 0);
        assert(x < size.width && y < size.height && z < size.depth);
        return values[z * (size.width * size.height) + y * size.width + x];
}

~TensorGradient() {

        for(int i=0; i < this->size.width * this->size.height * this->size.depth; i++) {
                delete values[i];
        }
        delete[] values;
}
};

// Layer abstract class
class Layer {

public:

LayerType type;
TensorFloat *input_gradients;
TensorFloat *input;
TensorFloat *output;
size_t input_size;
size_t output_size;
LayerGridFrameBuffer *gridRenderFrameBuffer;

virtual void activate(TensorFloat*)=0;
virtual void activate()=0;
virtual void calc_grads(TensorFloat*)=0;
virtual void fix_weights()=0;

};

class ConvolutionalLayer : public Layer {

public:

vector<TensorFloat*> filters;
vector<TensorGradient*> filter_gradients;
int stride, extend_filter;

ConvolutionalLayer(int stride, int extend_filter, int number_filters, size_t in_size) {
        cout << "Initializing ConvolutionalLayer...\n";
        type = LayerType::convolutional;
        input_size = in_size;

        //Define and alloc a grid layout for render buffers

        // {input, filter, gradient, output}
        // {cellObj, cellObj, cellObj, cellObj}
        // {cellObj, cellObj, cellObj, cellObj}
        // ...
        // {cellObj, cellObj, cellObj, cellObj}

        gridRenderFrameBuffer = new LayerGridFrameBuffer(3, number_filters, "Convolutional"); // 3 = {input, filter, gradient, output}

        // Initialize first column of the grid with Inputs buffers
        gridRenderFrameBuffer->column_titles.push_back("in");
        char *subtitle = new char[50];
        sprintf(subtitle, "%d x %d", in_size.width, in_size.height);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);

        for(int i=0; i<number_filters; i++) {
                gridRenderFrameBuffer->set(0, i, new TensorRenderFrameBuffer(in_size.width, in_size.height));
        }

        // Initialize second column of the grid with Filters buffers
        gridRenderFrameBuffer->column_titles.push_back("filter");
        subtitle = new char[50];
        sprintf(subtitle, "%d x %d", extend_filter, extend_filter);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);

        for(int i=0; i<number_filters; i++) {
                gridRenderFrameBuffer->set(1, i, new TensorRenderFrameBuffer(extend_filter, extend_filter));
        }

        // Initialize forth column of the grid with Output buffers
        gridRenderFrameBuffer->column_titles.push_back("out");
        subtitle = new char[50];
        sprintf(subtitle, "%d x %d", (in_size.width - extend_filter) / stride + 1, (in_size.height - extend_filter) / stride + 1);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);

        for(int i=0; i<number_filters; i++) {
                gridRenderFrameBuffer->set(2, i, new TensorRenderFrameBuffer((in_size.width - extend_filter) / stride + 1, (in_size.height - extend_filter) / stride + 1));
        }

        cout << "Creating tensor for input gradients...\n";
        input_gradients = new TensorFloat(in_size.width, in_size.height, in_size.depth);
        cout << "Creating tensor for input values...\n";
        input = new TensorFloat(in_size.width, in_size.height, in_size.depth);
        cout << "Creating tensor for output values...\n";
        output = new TensorFloat((in_size.width - extend_filter) / stride + 1, (in_size.height - extend_filter) / stride + 1, number_filters);
        this->stride = stride;
        this->extend_filter = extend_filter;
        assert( (float( in_size.width - extend_filter ) / stride + 1) == ((in_size.width - extend_filter) / stride + 1) );
        assert( (float( in_size.height - extend_filter ) / stride + 1) == ((in_size.height - extend_filter) / stride + 1) );

        TensorRenderFrameBuffer* filterFrameBuffer;

        for(int a = 0; a < number_filters; a++) {
                filterFrameBuffer = gridRenderFrameBuffer->get(1, a);
                cout << "Creating tensor for filter #" << a << " ...\n";
                TensorFloat *filter = new TensorFloat(extend_filter, extend_filter, in_size.depth);
                int maxval = extend_filter * extend_filter * in_size.depth;

                for(int x = 0; x < extend_filter; x++)
                {
                        for(int y = 0; y < extend_filter; y++)
                        {
                                for(int z = 0; z < in_size.depth; z++)
                                {
                                        float value = 1.0f / maxval * rand() / float( RAND_MAX );
                                        cout << "V: " << value << "\n";
                                        (*filter)(x, y, z) = value;
                                        filterFrameBuffer->set(x, y, (int)(value * 255));
                                }
                        }
                }

                filters.push_back(filter);
                filterFrameBuffer->swapBuffers();
        }

        for(int i = 0; i < number_filters; i++) {
                cout << "Creating tensor gradient for gradient #" << i << " ...\n";
                TensorGradient *tensorGradient = new TensorGradient(extend_filter, extend_filter, in_size.depth);

                // Update render frame gradients buffer values
                for(int x = 0; x < extend_filter; x++)
                {
                        for(int y = 0; y < extend_filter; y++)
                        {
                                for(int z = 0; z < in_size.depth; z++)
                                {
                                        Gradient *gradient = tensorGradient->get(x, y, z);
                                        float value = gradient->grad;
                                }
                        }
                }

                filter_gradients.push_back(tensorGradient);
        }

}

point_t map_to_input(point_t out, int z) {
        out.x *= stride;
        out.y *= stride;
        out.z = z;
        return out;
}

int normalize_range(float f, int max, bool lim_min) {
        if(f <= 0) { return 0; }

        max -= 1;

        if(f >= max) { return max; }

        if(lim_min) {
                // left side of inequality
                return ceil( f );
        } else {
                return floor( f );
        }
}

range_t map_to_output(int x, int y) {
        float a = x;
        float b = y;
        return {
                       normalize_range( (a - extend_filter + 1) / stride, output->size.width, true ),
                       normalize_range( (b - extend_filter + 1) / stride, output->size.height, true ),
                       0,
                       normalize_range( a / stride, output->size.width, false ),
                       normalize_range( b / stride, output->size.height, false ),
                       (int)filters.size() - 1,
        };
}

void activate(TensorFloat *in) {
        this->input = in;

        // Update render frame inputs buffer values
        for(int filter = 0; filter < filters.size(); filter++)
        {
                TensorRenderFrameBuffer* inputFrameBuffer = gridRenderFrameBuffer->get(0, filter);
                for(int x = 0; x < in->size.width; x++)
                {
                        for(int y = 0; y < in->size.height; y++)
                        {
                                for(int z = 0; z < in->size.depth; z++)
                                {
                                        float value = in->get(x, y, z);
                                        inputFrameBuffer->set(x, y, (int)(value * 255));
                                }
                        }
                }
                inputFrameBuffer->swapBuffers();
        }

        // Activate
        activate();
}

void activate() {

        for(int filter = 0; filter < filters.size(); filter++)
        {
                TensorRenderFrameBuffer* outputFrameBuffer = gridRenderFrameBuffer->get(2, filter);
                TensorFloat *filter_data = filters[filter];
                for(int y = 0; y < output->size.height; y++)
                {
                        for(int x = 0; x < output->size.width; x++)
                        {
                                point_t mapped = map_to_input( { (uint16_t)x, (uint16_t)y, 0 }, 0 );
                                float sum = 0;
                                for(int i = 0; i < extend_filter; i++)
                                {
                                        for(int j = 0; j < extend_filter; j++)
                                        {
                                                for(int z = 0; z < input->size.depth; z++)
                                                {
                                                        float f = (*filter_data)( i, j, z );
                                                        float v = (*input)( mapped.x + i, mapped.y + j, z );
                                                        sum += f*v;
                                                }
                                        }
                                }
                                (*output)(x, y, filter) = sum;
                                outputFrameBuffer->set(x, y, (int)(sum * 255));
                        }
                }
                outputFrameBuffer->swapBuffers();
        }

}

void fix_weights() {

        TensorRenderFrameBuffer* filterFrameBuffer;
        for(int k = 0; k < filters.size(); k++)
        {
                filterFrameBuffer = gridRenderFrameBuffer->get(1, k);
                for(int y = 0; y < extend_filter; y++)
                {
                        for(int x = 0; x < extend_filter; x++)
                        {
                                for(int z = 0; z < input->size.depth; z++)
                                {
                                        TensorFloat *filter = filters[k];
                                        float& w = filter->get(x, y, z);
                                        TensorGradient *tensor_gradient = filter_gradients[k];
                                        Gradient *grad = tensor_gradient->get(x, y, z);
                                        w = update_weight(w, grad);
                                        update_gradient(grad);
                                        filterFrameBuffer->set128(x, y, (int)((w * 128)/0.5f)); // signed value between -128 and 128
                                }
                        }
                }
                filterFrameBuffer->swapBuffers();
        }

}

void calc_grads(TensorFloat* grad_next_layer) {

        // Reset all layer gradients to 0
        for (int k = 0; k < filter_gradients.size(); k++) {
                TensorGradient *gradient = filter_gradients[k]; //gradient->get(x, y, z).grad;

                for ( int x = 0; x < extend_filter; x++ ) {
                        for ( int y = 0; y < extend_filter; y++ ) {
                                for ( int z = 0; z < input->size.depth; z++ ) {
                                        gradient->get(x, y, z)->grad = 0;
                                }
                        }
                }
        }

        for(int x = 0; x < input->size.width; x++) {
                for(int y = 0; y < input->size.height; y++) {
                        range_t rn = map_to_output(x, y);
                        for(int z = 0; z < input->size.depth; z++) {
                                float sum_error = 0;
                                for(int i = rn.min_x; i <= rn.max_x; i++) {
                                        int minx = i * stride;
                                        for(int j = rn.min_y; j <= rn.max_y; j++) {
                                                int miny = j * stride;
                                                for(int k = 0; k < filters.size(); k++) {
                                                        TensorGradient *tensorGradient = filter_gradients[k];
                                                        TensorFloat *tensorFilter = filters[k];
                                                        int w_applied = tensorFilter->get( x - minx, y - miny, z );
                                                        sum_error += w_applied * (*grad_next_layer)( i, j, k );
                                                        float value = (*input)( x, y, z ) * (*grad_next_layer)( i, j, k );

                                                        Gradient *gradient = tensorGradient->get(x - minx, y - miny, z);
                                                        gradient->grad += value;
                                                }
                                        }
                                }
                                (*input_gradients)(x, y, z) = sum_error;
                        }
                }
        }

}

~ConvolutionalLayer() {
        for(int f=0; f<filters.size(); f++)
                delete filters[f];

        for(int i=0; i<filter_gradients.size(); i++)
                delete filter_gradients[i];
        delete input_gradients;
        delete input;
        delete output;
        delete gridRenderFrameBuffer;
//TODO: Implement proper delete allocated filters


}

};

class ReLuLayer : public Layer {

public:

ReLuLayer(size_t in_size) {
        cout << "Initializing ReLuLayer...\n";
        type = LayerType::relu;
        input_size = in_size;

        //Define and alloc a grid layout for render buffers

        // {input, output}
        // {cellObj, cellObj}

        gridRenderFrameBuffer = new LayerGridFrameBuffer(2, in_size.depth, "ReLu"); // 2 = {input, output}

        // Initialize first column of the grid with Inputs buffers
        gridRenderFrameBuffer->column_titles.push_back("in");
        char* subtitle = new char[50];
        sprintf(subtitle, "%d x %d", in_size.width, in_size.height, in_size.depth);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);

        for(int i=0; i<in_size.depth; i++) {
                gridRenderFrameBuffer->set(0, i, new TensorRenderFrameBuffer(in_size.width, in_size.height));
        }

        // Initialize forth column of the grid with Output buffers
        gridRenderFrameBuffer->column_titles.push_back("out");
        subtitle = new char[50];
        sprintf(subtitle, "%d x %d", in_size.width, in_size.height);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);

        for(int i=0; i<in_size.depth; i++) {
                gridRenderFrameBuffer->set(1, i, new TensorRenderFrameBuffer(in_size.width, in_size.height));
        }

        cout << "Creating tensor for input gradients...\n";
        input_gradients = new TensorFloat(in_size.width, in_size.height, in_size.depth);
        cout << "Creating tensor for input values...\n";
        input = new TensorFloat(in_size.width, in_size.height, in_size.depth);
        cout << "Creating tensor for output values...\n";
        output = new TensorFloat(in_size.width, in_size.height, in_size.depth);
}

void activate(TensorFloat *in) {
        this->input = in;

        for(int z = 0; z < in->size.depth; z++)
        {
                // Update render frame input buffer values
                TensorRenderFrameBuffer* inputFrameBuffer = gridRenderFrameBuffer->get(0, z);
                for(int x = 0; x < in->size.width; x++)
                {
                        for(int y = 0; y < in->size.height; y++)
                        {
                                float value = in->get(x, y, z);
                                inputFrameBuffer->set(x, y, (int)(value * 255));
                        }
                }
                inputFrameBuffer->swapBuffers();
        }

        // Activate
        activate();
}

void activate() {

        for(int z = 0; z < input->size.depth; z++)
        {
                TensorRenderFrameBuffer* outputFrameBuffer = gridRenderFrameBuffer->get(1, z);
                for(int x = 0; x < input->size.width; x++)
                {
                        for(int y = 0; y < input->size.height; y++)
                        {

                                float value = (*input)(x, y, z); //in(x, y, z);
                                if(value < 0)
                                        value = 0;
                                (*output)(x, y, z) = value;
                                outputFrameBuffer->set(x, y, (int)(value * 255));
                        }
                }
                outputFrameBuffer->swapBuffers();
        }
}

void fix_weights() {

}

void calc_grads(TensorFloat* grad_next_layer) {

        for(int x = 0; x < input->size.width; x++)
        {
                for(int y = 0; y < input->size.height; y++)
                {
                        for(int z = 0; z < input->size.depth; z++)
                        {
                                (*input_gradients)(x, y, z) = ((*input)(x, y, z) < 0) ? 0 : (1 * (*grad_next_layer)(x, y, z));
                        }
                }
        }

}

~ReLuLayer() {
        delete gridRenderFrameBuffer;
        delete input_gradients;
        delete input;
        delete output;

//TODO: Implement proper delete allocated filters


}

};

class PoolLayer : public Layer {

public:

vector<TensorGradient*> filter_gradients;
int stride, extend_filter;

PoolLayer(int stride, int extend_filter, size_t in_size) {
        cout << "Initializing PoolLayer...\n";
        type = LayerType::pool;
        input_size = in_size;

        //Define and alloc a grid layout for render buffers

        // {input, gradient, output}
        // {cellObj, cellObj, cellObj}

        gridRenderFrameBuffer = new LayerGridFrameBuffer(3, in_size.depth, "Pool"); // 3 = {input, gradient, output}

        // Initialize first column of the grid with Inputs buffers
        gridRenderFrameBuffer->column_titles.push_back("in");
        char* subtitle = new char[50];
        sprintf(subtitle, "%d x %d", in_size.width, in_size.height);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);

        for(int i=0; i<in_size.depth; i++) {
                gridRenderFrameBuffer->set(0, i, new TensorRenderFrameBuffer(in_size.width, in_size.height));
        }

        // Initialize third column of the grid with Gradients buffers
        gridRenderFrameBuffer->column_titles.push_back("grad");
        subtitle = new char[50];
        sprintf(subtitle, "%d x %d", in_size.width, in_size.height);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);

        for(int i=0; i<in_size.depth; i++) {
                gridRenderFrameBuffer->set(1, i, new TensorRenderFrameBuffer(in_size.width, in_size.height));
        }

        // Initialize forth column of the grid with Output buffers
        gridRenderFrameBuffer->column_titles.push_back("out");
        subtitle = new char[50];
        sprintf(subtitle, "%d x %d", (in_size.width - extend_filter) / stride + 1, (in_size.height - extend_filter) / stride + 1);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);

        for(int i=0; i<in_size.depth; i++) {
                gridRenderFrameBuffer->set(2, i, new TensorRenderFrameBuffer((in_size.width - extend_filter) / stride + 1, (in_size.height - extend_filter) / stride + 1));
        }

        input_gradients = new TensorFloat(in_size.width, in_size.height, in_size.depth);
        input = new TensorFloat(in_size.width, in_size.height, in_size.depth);
        output = new TensorFloat((in_size.width - extend_filter) / stride + 1, (in_size.height - extend_filter) / stride + 1, in_size.depth);
        this->stride = stride;
        this->extend_filter = extend_filter;
        assert( (float( in_size.width - extend_filter ) / stride + 1) == ((in_size.width - extend_filter) / stride + 1) );
        assert( (float( in_size.height - extend_filter ) / stride + 1) == ((in_size.height - extend_filter) / stride + 1) );
}

point_t map_to_input(point_t out, int z) {
        out.x *= stride;
        out.y *= stride;
        out.z = z;
        return out;
}

int normalize_range(float f, int max, bool lim_min) {
        if(f <= 0) { return 0; }

        max -= 1;

        if(f >= max) { return max; }

        if(lim_min) {
                // left side of inequality
                return ceil( f );
        } else {
                return floor( f );
        }
}

range_t map_to_output(int x, int y) {
        float a = x;
        float b = y;
        return {
                       normalize_range( (a - extend_filter + 1) / stride, output->size.width, true ),
                       normalize_range( (b - extend_filter + 1) / stride, output->size.height, true ),
                       0,
                       normalize_range( a / stride, output->size.width, false ),
                       normalize_range( b / stride, output->size.height, false ),
                       (int)output->size.depth - 1,
        };
}

void activate(TensorFloat *in) {
        this->input = in;

        for(int z = 0; z < in->size.depth; z++)
        {
                // Update render frame inputs buffer values
                TensorRenderFrameBuffer* inputFrameBuffer = gridRenderFrameBuffer->get(0, z);
                for(int x = 0; x < in->size.width; x++)
                {
                        for(int y = 0; y < in->size.height; y++)
                        {
                                float value = in->get(x, y, z);
                                inputFrameBuffer->set(x, y, (int)(value * 255));
                        }
                }
                inputFrameBuffer->swapBuffers();
        }

        // Activate
        activate();
}

void activate() {

        for(int z = 0; z < output->size.depth; z++)
        {
                TensorRenderFrameBuffer* outputFrameBuffer = gridRenderFrameBuffer->get(2, z);
                for(int x = 0; x < output->size.width; x++)
                {
                        for(int y = 0; y < output->size.height; y++)
                        {
                                point_t mapped = map_to_input( { (uint16_t)x, (uint16_t)y, 0 }, 0 );
                                float mval = -FLT_MAX;
                                for(int i = 0; i < extend_filter; i++)
                                        for(int j = 0; j < extend_filter; j++)
                                        {
                                                float v = (*input)(mapped.x + i, mapped.y + j, z);
                                                if(v > mval)
                                                        mval = v;
                                        }
                                (*output)(x, y, z) = mval;
                                outputFrameBuffer->set(x, y, (int)(mval * 255));
                        }
                }
                outputFrameBuffer->swapBuffers();
        }

}

void fix_weights() {

}

void calc_grads(TensorFloat* grad_next_layer) {

        for(int y = 0; y < input_size.height; y++)
        {
                for(int x = 0; x < input_size.width; x++)
                {
                        range_t rn = map_to_output(x, y);
                        for(int z = 0; z < input_size.depth; z++)
                        {
                                TensorRenderFrameBuffer* gradientFrameBuffer = gridRenderFrameBuffer->get(1, z);
                                float sum_error = 0;
                                for(int i = rn.min_x; i <= rn.max_x; i++)
                                {
                                        int minx = i * stride;
                                        for(int j = rn.min_y; j <= rn.max_y; j++)
                                        {
                                                int miny = j * stride;
                                                int is_max = (*input)(x, y, z) == (*output)(i, j, z) ? 1 : 0;
                                                sum_error += is_max * (*grad_next_layer)(i, j, z);
                                        }
                                }
                                if(z==0) {
                                  cout << " " << sum_error;
                                }
                                (*input_gradients)(x, y, z) = sum_error;
                                gradientFrameBuffer->set(x, y, (int)sum_error);
                        }
                }
                cout << "\n";
        }

        cout << "\n------------\n";

        for(int i=0; i<input_size.depth; i++) {
          TensorRenderFrameBuffer* gradientFrameBuffer = gridRenderFrameBuffer->get(1, i);
          gradientFrameBuffer->swapBuffers();
        }

}

~PoolLayer() {
        delete gridRenderFrameBuffer;
        delete input_gradients;
        delete input;
        delete output;
//TODO: Implement proper delete allocated filters

}

};


class FullyConnectedLayer : public Layer {

public:

TensorFloat *weights;
vector<float> input_vector;
vector<Gradient> gradients;

FullyConnectedLayer(size_t in_size, size_t out_size) {
        cout << "Initializing FullyConnectedLayer...\n";
        type = LayerType::fc;
        input_size = in_size;
        output_size = out_size;

        //Define and alloc a grid layout for render buffers

        // {input, gradient, output}
        // {cellObj, cellObj, cellObj}

        gridRenderFrameBuffer = new LayerGridFrameBuffer(3, 1, "FullyConnected"); // 3 = {input, gradient, output}

        // Initialize first column of the grid with Inputs buffers
        gridRenderFrameBuffer->column_titles.push_back("in");
        char *subtitle = new char[50];
        sprintf(subtitle, "%d x %d", in_size.width, in_size.height);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);
        gridRenderFrameBuffer->set(0, 0, new TensorRenderFrameBuffer(in_size.width, in_size.height));


        // Initialize third column of the grid with Gradients buffers
        gridRenderFrameBuffer->column_titles.push_back("grad");
        subtitle = new char[50];
        sprintf(subtitle, "%d x %d", in_size.width, in_size.height);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);
        gridRenderFrameBuffer->set(1, 0, new TensorRenderFrameBuffer(in_size.width, in_size.height));


        // Initialize forth column of the grid with Output buffers
        gridRenderFrameBuffer->column_titles.push_back("out");
        subtitle = new char[50];
        sprintf(subtitle, "%d x %d", out_size.width, out_size.height);
        gridRenderFrameBuffer->column_subtitles.push_back(subtitle);
        gridRenderFrameBuffer->set(2, 0, new TensorRenderFrameBuffer(out_size.width, out_size.height));


        input_gradients = new TensorFloat(in_size.width, in_size.height, in_size.depth);
        gradients = vector<Gradient>(output_size.width);
        input_vector = vector<float>(output_size.width);
        input = new TensorFloat(in_size.width, in_size.height, in_size.depth);
        output = new TensorFloat(out_size.width, out_size.height, out_size.depth);
        weights = new TensorFloat(in_size.width * in_size.height * in_size.depth, out_size.width, out_size.height);

        int maxval = in_size.width * in_size.height * in_size.depth;

        for(int i = 0; i < out_size.width; i++) {
                for(int h = 0; h < in_size.width * in_size.height * in_size.depth; h++) {
                        (*weights)(h, i, 0) = 2.19722f / maxval * rand() / float( RAND_MAX );
                }
        }
        // 2.19722f = f^-1(0.9) => x where [1 / (1 + exp(-x) ) = 0.9]

}

float activator_function(float x)
{
        //return tanhf( x );
        float sig = 1.0f / (1.0f + exp( -x ));
        return sig;
}

float activator_derivative(float x)
{
        //float t = tanhf( x );
        //return 1 - t * t;
        float sig = 1.0f / (1.0f + exp( -x ));
        return sig * (1 - sig);
}

int map(point_t d)
{
        return d.z * (input->size.width * input->size.height) + d.y * (input->size.width) + d.x;
}

void activate(TensorFloat *in) {

        this->input = in;

        // Update render frame inputs buffer values
        TensorRenderFrameBuffer* inputFrameBuffer = gridRenderFrameBuffer->get(0, 0);
        for(int x = 0; x < in->size.width; x++)
        {
                for(int y = 0; y < in->size.height; y++)
                {
                        for(int z = 0; z < in->size.depth; z++)
                        {
                                float value = in->get(x, y, z);
                                inputFrameBuffer->set(x, y, (int)(value * 255));
                        }
                }
        }

        inputFrameBuffer->swapBuffers();

        // Activate
        activate();
}

void activate() {

        TensorRenderFrameBuffer* outputFrameBuffer = gridRenderFrameBuffer->get(2, 0);
        for(int n = 0; n < output->size.width; n++)
        {
                float inputv = 0;
                for(int i = 0; i < input->size.width; i++)
                {
                        for(int j = 0; j < input->size.height; j++)
                        {
                                for(int z = 0; z < input->size.depth; z++)
                                {
                                        int m = map( { i, j, z } );
                                        inputv += (*input)(i, j, z) * (*weights)(m, n, 0);
                                }
                        }
                }

                input_vector[n] = inputv;
                float value = activator_function(inputv);
                (*output)(n, 0, 0) = value;
                outputFrameBuffer->set(n, 0, (int)(value * 255));
        }
        outputFrameBuffer->swapBuffers();
}

void fix_weights() {

        for(int n = 0; n < output->size.width; n++) {

                Gradient &grad = gradients[n];

                for(int i = 0; i < input->size.width; i++) {
                        for(int j = 0; j < input->size.height; j++) {
                                for(int z = 0; z < input->size.depth; z++) {
                                        int m = map( { i, j, z } );
                                        float &w = (*weights)(m, n, 0);
                                        w = update_weight(w, &grad, (*input)(i, j, z));
                                }
                        }
                }

                update_gradient(&grad);
        }

}

void calc_grads(TensorFloat* grad_next_layer) {

        memset(input_gradients->values, 0, input_gradients->size.width * input_gradients->size.height * input_gradients->size.depth * sizeof(float));
        for(int n = 0; n < output->size.width; n++)
        {
                Gradient& grad = gradients[n];
                grad.grad = (*grad_next_layer)(n, 0, 0) * activator_derivative(input_vector[n]);

                for(int i = 0; i < input->size.width; i++) {
                        for(int j = 0; j < input->size.height; j++) {
                                for(int z = 0; z < input->size.depth; z++) {
                                        int m = map( { i, j, z } );
                                        (*input_gradients)(i, j, z) += grad.grad * (*weights)(m, n, 0);
                                }
                        }
                }

        }

}

~FullyConnectedLayer() {

        delete gridRenderFrameBuffer;
        delete input_gradients;
        delete input;
        delete output;
//TODO: Implement proper delete allocated filters


}

};


class Network {
vector<Layer*> layers;

public:

Network() {

}

};

class InputCase {

public:

TensorFloat *data;
TensorFloat *output;

InputCase(size_t data_size, size_t out_size) {
        data = new TensorFloat(data_size.width, data_size.height, data_size.depth);
        output = new TensorFloat(out_size.width, out_size.height, out_size.depth);
}

~InputCase() {
        delete data;
        delete output;
}

};


}

/***********************************/


using namespace NeuralNetwork;

struct point_t
{
        int x, y, z;
};

uint32_t byteswap_uint32(uint32_t a)
{
        return ((((a >> 24) & 0xff) << 0) |
                (((a >> 16) & 0xff) << 8) |
                (((a >> 8) & 0xff) << 16) |
                (((a >> 0) & 0xff) << 24));
}

uint8_t* read_file( const char* szFile )
{
        ifstream file( szFile, ios::binary | ios::ate );
        streamsize size = file.tellg();
        file.seekg( 0, ios::beg );

        if ( size == -1 )
                return nullptr;

        uint8_t* buffer = new uint8_t[size];
        file.read( (char*)buffer, size );
        return buffer;
}

void drawString(int x, int y, char* msg, void *font = GLUT_BITMAP_HELVETICA_10) {
        glColor3d(0.0, 0.0, 0.0);
        glRasterPos2d(x, HEIGHT - y);
        for (const char *c = msg; *c != '\0'; c++) {
                glutBitmapCharacter(font, *c);
        }
}

GLuint LoadTextureWithTensorRenderFrameBuffer(TensorRenderFrameBuffer *tensorFrameBuffer)
{
        tensorFrameBuffer->consumer_mutex.lock();
        tensorFrameBuffer->is_consuming_frame_buffer = true;

        if(tensorFrameBuffer->consumer_frame_buffer == NULL) {
                return NULL;
        }

        if(tensorFrameBuffer->texture != NULL) {
                // Delete previous generated texture
                glDeleteTextures(1, &tensorFrameBuffer->texture);
        }

        glGenTextures( 1, &tensorFrameBuffer->texture );
        glBindTexture( GL_TEXTURE_2D, tensorFrameBuffer->texture );
        glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        gluBuild2DMipmaps( GL_TEXTURE_2D, GL_RGBA, tensorFrameBuffer->texture_width, tensorFrameBuffer->texture_height, GL_RGBA, GL_UNSIGNED_BYTE, tensorFrameBuffer->consumer_frame_buffer);

        tensorFrameBuffer->is_consuming_frame_buffer = false;
        tensorFrameBuffer->consumer_mutex.unlock();

        return tensorFrameBuffer->texture;
}

GLuint LoadTexture()
{
        if(consumer_frame_buffer == NULL) {
                return NULL;
        }

        GLuint texture;
        int width, height;

        width = 28;
        height = 28;

        glGenTextures( 1, &texture );
        glBindTexture( GL_TEXTURE_2D, texture );
        glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

        gluBuild2DMipmaps( GL_TEXTURE_2D, GL_RGB, width, height, GL_RGB, GL_UNSIGNED_BYTE, consumer_frame_buffer);

        return texture;
}

void display(void)
{
        frame_counter++;

        glClear(GL_COLOR_BUFFER_BIT);

        if(glutGet(GLUT_ELAPSED_TIME) - timebase_timestamp > 1000) {
                int current_timestamp = glutGet(GLUT_ELAPSED_TIME);
                snprintf(current_fps_buffer, 20, "%4.1ffps", (frame_counter * 1000.0)/(current_timestamp - timebase_timestamp));
                timebase_timestamp = current_timestamp;
                frame_counter = 0;
        }

        glColor3f(1,1,1);
/*
        consumer_mutex.lock();
        is_consuming_frame_buffer = true;
        GLuint texture = LoadTexture();
        is_consuming_frame_buffer = false;
        consumer_mutex.unlock();

        if(texture != NULL) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, texture);
                glBegin(GL_QUADS);
                glTexCoord2f(0, 0);
                glVertex2f(0, HEIGHT - 0);
                glTexCoord2f(0, 1);
                glVertex2f(0, HEIGHT - 100);
                glTexCoord2f(1, 1);
                glVertex2f(100, HEIGHT - 100);
                glTexCoord2f(1, 0);
                glVertex2f(100, HEIGHT - 0);
                glEnd();
                glDisable(GL_TEXTURE_2D);
        }
 */

        glEnable(GL_TEXTURE_2D);
        int x_offset = 20;
        int y_offset = 0;
        for(Layer* layer: layers) {
                LayerGridFrameBuffer *grid = layer->gridRenderFrameBuffer;

                x_offset += 50; // horizontal space between layers

                drawString(x_offset + 10, 25, grid->title, GLUT_BITMAP_HELVETICA_18);
                glColor3f(1,1,1);

                for(int x=0; x<grid->width; x++) {

                        x_offset += 10;
                        y_offset = 44;

                        drawString(x_offset, 50, grid->column_titles[x], GLUT_BITMAP_HELVETICA_10);
                        drawString(x_offset, 65, grid->column_subtitles[x], GLUT_BITMAP_TIMES_ROMAN_10);
                        glColor3f(1,1,1);

                        for(int y=0; y<grid->height; y++) {

                                y_offset += 24;
                                TensorRenderFrameBuffer* tensorFrameBuffer = grid->get(x, y);
                                GLuint texture = LoadTextureWithTensorRenderFrameBuffer(tensorFrameBuffer);
                                int tile_width = (tensorFrameBuffer->texture_width * 30) / tensorFrameBuffer->width;
                                int tile_height = (tensorFrameBuffer->texture_height * 30) / tensorFrameBuffer->height;

                                if(texture != NULL) {
                                        glBindTexture(GL_TEXTURE_2D, texture);
                                        glBegin(GL_QUADS);
                                        glTexCoord2f(0, 0);
                                        glVertex2f(x_offset, HEIGHT - y_offset);
                                        glTexCoord2f(0, 1);
                                        glVertex2f(x_offset, HEIGHT - y_offset - tile_height);
                                        glTexCoord2f(1, 1);
                                        glVertex2f(x_offset + tile_width, HEIGHT - y_offset - tile_height);
                                        glTexCoord2f(1, 0);
                                        glVertex2f(x_offset + tile_width, HEIGHT - y_offset);
                                        glEnd();
                                }

                                y_offset += 30;
                        }
                        x_offset += 30;
                }
        }
        glDisable(GL_TEXTURE_2D);

        drawString(WIDTH - 45, 15, current_fps_buffer, GLUT_BITMAP_HELVETICA_10);
        snprintf(avg_error_percent_buffer, 30, "%4.5f Avg. error", avg_error_percent);
        drawString(WIDTH - 220, HEIGHT - 18, avg_error_percent_buffer, GLUT_BITMAP_TIMES_ROMAN_24);

        glFlush();
        glutSwapBuffers();
}

vector<InputCase*> read_test_cases()
{
        vector<InputCase*> cases;

        producer_frame_buffer = (unsigned char *)malloc((28 * 28 * 3));
        consumer_frame_buffer = (unsigned char *)malloc((28 * 28 * 3));

        uint8_t* train_image = read_file( "train-images.idx3-ubyte" );
        uint8_t* train_labels = read_file( "train-labels.idx1-ubyte" );
        uint32_t case_count = byteswap_uint32( *(uint32_t*)(train_image + 4) );

        for(int i = 0; i < case_count; i++)
        {
                NeuralNetwork::size_t input_size{28, 28, 1};
                NeuralNetwork::size_t output_size{10, 1, 1};

                InputCase *c = new InputCase(input_size, output_size);

                uint8_t* img = train_image + 16 + i * (28 * 28);
                uint8_t* label = train_labels + 8 + i;

                for ( int x = 0; x < 28; x++ )
                        for ( int y = 0; y < 28; y++ ) {
                                (*c->data)(x, y, 0) = img[x + y * 28] / 255.f;
                                producer_frame_buffer[x * 3 + y * 28 * 3] = 0;
                                producer_frame_buffer[x * 3 + y * 28 * 3 + 1] = img[x + y * 28]; // green byte
                                producer_frame_buffer[x * 3 + y * 28 * 3 + 2] = 0;
                        }

                if(!is_consuming_frame_buffer) {
                        consumer_mutex.lock();
                        unsigned char *tmp = producer_frame_buffer;
                        producer_frame_buffer = consumer_frame_buffer;
                        consumer_frame_buffer = tmp;
                        consumer_mutex.unlock();
                }

                for ( int b = 0; b < 10; b++ ) {
                        (*c->output)(b, 0, 0) = *label == b ? 1.0f : 0.0f;
                }

                cases.push_back(c);
        }

        delete[] train_image;
        delete[] train_labels;

        return cases;
}

void idle(void)
{
        glutPostRedisplay();
}

void reshape(int w, int h)
{
        glViewport(0, 0, WIDTH, HEIGHT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluOrtho2D(0.0, (GLdouble) w, 0.0, (GLdouble) h);
}

float train(vector<Layer*> &layers, InputCase *input_case)//TensorFloat *data, TensorFloat *expected)
{
        for(int i = 0; i < layers.size(); i++) {
                Layer *layer = layers[i];

                if(i == 0) { layer->activate(input_case->data); }
                else       {
//                        cout << "Output layer " << i << " - X: " << layers[i - 1]->output->size.width << " Y: " << layers[i - 1]->output->size.height << " Z: " << layers[i - 1]->output->size.depth << "\n";
                        layer->activate(layers[i - 1]->output);
                }
        }

        //output of the last layer must have the same size as the case expected size
        TensorFloat* diff_gradient = TensorFloat::diff(layers.back()->output, input_case->output); // difference between the neural network output and expected output

        for(int i = layers.size() - 1; i >= 0; i--) {
                if(i == layers.size() - 1)  { layers[i]->calc_grads(diff_gradient); }
                else                        { layers[i]->calc_grads(layers[i + 1]->input_gradients); }
        }

        //WARNING: following massive calls to fix_weights causes memory leaks
        for(int i = 0; i < layers.size(); i++) {
                layers[i]->fix_weights();
        }

        float err = 0;

        //check if the output of the last layer have the same size as the case expected size
        if((diff_gradient->size.width == input_case->output->size.width) && (diff_gradient->size.height == input_case->output->size.height) && (diff_gradient->size.depth == input_case->output->size.depth)) {
                //calculate the error %
                for(int i = 0; i < diff_gradient->size.width * diff_gradient->size.height * diff_gradient->size.depth; i++) {
                        float f = input_case->output->values[i];
                        if(f > 0.5)
                                err += abs(diff_gradient->values[i]);
                }
        }

        delete diff_gradient;

        return err * 100;
}

static void* tensarThreadFunc(void* v) {
        vector<InputCase*> cases = read_test_cases();
        /*
               // stride, filter_width(extend_filter), num_filters, filter_size
               ConvolutionalLayer *cnn_layer = new ConvolutionalLayer(1, 5, 8, cases[0]->data->size);    // 28 * 28 * 1 -> 24 * 24 * 8
               ReLuLayer *relu_layer = new ReLuLayer(cnn_layer->output->size);    // 28 * 28 * 1 -> 24 * 24 * 8
               PoolLayer *pool_layer = new PoolLayer(2, 2, relu_layer->output->size);
               FullyConnectedLayer *fc_layer = new FullyConnectedLayer(pool_layer->output->size, {10, 1, 1});

               layers.push_back(cnn_layer);
               layers.push_back(relu_layer);
               layers.push_back(pool_layer);
               layers.push_back(fc_layer);
         */

        ConvolutionalLayer *cnn_layer1 = new ConvolutionalLayer(1, 5, 8, cases[0]->data->size); // 28 * 28 * 1 -> 24 * 24 * 8
        ReLuLayer *relu_layer1 = new ReLuLayer(cnn_layer1->output->size); // 28 * 28 * 1 -> 24 * 24 * 8
        PoolLayer *pool_layer1 = new PoolLayer(2, 2, relu_layer1->output->size);
/*
        ConvolutionalLayer *cnn_layer2 = new ConvolutionalLayer(1, 3, 10, pool_layer1->output->size); // 28 * 28 * 1 -> 24 * 24 * 8
        ReLuLayer *relu_layer2 = new ReLuLayer(cnn_layer2->output->size); // 28 * 28 * 1 -> 24 * 24 * 8
        PoolLayer *pool_layer2 = new PoolLayer(2, 2, relu_layer2->output->size);
*/
        FullyConnectedLayer *fc_layer = new FullyConnectedLayer(pool_layer1->output->size, {10, 1, 1});

        layers.push_back(cnn_layer1);
        layers.push_back(relu_layer1);
        layers.push_back(pool_layer1);
/*
        layers.push_back(cnn_layer2);
        layers.push_back(relu_layer2);
        layers.push_back(pool_layer2);
*/
        layers.push_back(fc_layer);

        float amse = 0;
        int ic = 0;

        cout << "Start training...\n";

        for(long ep = 0; ep < 100000;)
        {

                for(int i=0; i<cases.size(); i++)
                {
                        InputCase *input_case = cases[i];
                        float xerr = train(layers, input_case);
                        //cout << "Trained case #" << i << " - Error: " << xerr << "\n";
                        amse += xerr;

                        ep++;
                        ic++;
                        avg_error_percent = amse/ic;

                        if(ep % 1000 == 0) {
                                cout << "case " << ep << " err=" << avg_error_percent << endl;

                                TensorFloat* expected = input_case->output;
                                cout << "Expected:\n";
                                for(int e = 0; e < 10; e++) {
                                        printf("[%i] %f\n", e, (*expected)(e, 0, 0)*100.0f);
                                }

                                cout << "Output:\n";
                                TensorFloat* output = layers.back()->output;
                                for(int o = 0; o < 10; o++) {
                                        printf("[%i] %f\n", o, (*output)(o, 0, 0)*100.0f);
                                }
                        }
                }
        }
}

int main(int argc, char *argv[]) {

        //Network *network = new Network();

/*
        points.push_back(Point2d(20,20));
        points.push_back(Point2d(30,30));
        points.push_back(Point2d(40,40));
        points.push_back(Point2d(50,50));
        points.push_back(Point2d(60,60));
 */
        pthread_t tensarThreadId;
        pthread_create(&tensarThreadId, NULL, tensarThreadFunc, 0);

        glutInit(&argc, argv);
        glutInitDisplayMode(GLUT_RGB);
        glutInitWindowSize(WIDTH, HEIGHT);
        glutInitWindowPosition(0, 0);
        glutCreateWindow("Tensar");
        glutDisplayFunc(display);
        glutReshapeFunc(reshape);
        glutIdleFunc(idle);

        glClearColor(1.0, 1.0, 1.0, 1.0);
        //glEnable(GL_LINE_SMOOTH);

        glutMainLoop();

        return 0;
}