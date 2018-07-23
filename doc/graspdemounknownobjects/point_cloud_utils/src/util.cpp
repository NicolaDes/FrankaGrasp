// #include "point_cloud_utils/util.h"
//#define EIGEN_DONT_VECTORIZE
//#define EIGEN_DISABLE_UNALIGNED_ARRAY_ASSERT
#include <stdint.h>
#include "pcl/ros/register_point_struct.h"
#include "pcl/point_types.h"

namespace my_ns
{
    struct MyPoint
    {
       float x;
       float y;
       float z;
       float rgb;
       uint32_t index;
       float data_c[3]; // Added to align to 16 byte boundaries
       EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
    } EIGEN_ALIGN16;
} // namespace my_ns

// Must be used in the global namespace with the fully-qualified name
// of the point type.
POINT_CLOUD_REGISTER_POINT_STRUCT(
      my_ns::MyPoint,
      (float, x, x)
      (float, y, y)
      (float, z, z)
      (float, rgb, rgb)
      (uint32_t, index, index));


#include "util.h"
using namespace std;

// PCL objects
pcl::NormalEstimation < Point, pcl::Normal > n3d_;

static ros::Publisher chatter_pub;
static ros::Publisher chatter_pub2;
static ros::Publisher chatter_pub3;
static ros::Publisher chatter_pub4;
static ros::Publisher chatter_pub5;
dynamic_reconfigure::Server < point_cloud_utils::utilConfig > *srv;
//ros::ServiceClient client;
//ros::ServiceClient client2;
//adept_server::AnglesRequest anglesRequest;
//arm_geometry::TFMatRequest tfMatRequest;
sensor_msgs::CvBridge g_bridge;
boost::mutex global_mutex;

// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/** \brief Deep cloning of PointCloud2 message
 * \param input the message in the sensor_msgs::PointCloud2 format
 * \param output the resultant message in the sensor_msgs::PointCloud2 format
 */
void
clone_PointCloud2(const sensor_msgs::PointCloud2 & input,
                  sensor_msgs::PointCloud2 & output)
{
  output.header = input.header;
  output.width = input.width;
  output.height = input.height;
  output.fields.resize(input.fields.size());
  // Copy all fields
  for (size_t d = 0; d < input.fields.size(); ++d)
  {
    output.fields[d].name = input.fields[d].name;
    output.fields[d].offset = input.fields[d].offset;
    output.fields[d].datatype = input.fields[d].datatype;
    output.fields[d].count = input.fields[d].count;
  }
  output.point_step = input.point_step; // add 4 bytes for index field
  output.row_step = output.point_step * output.width;

  output.is_bigendian = input.is_bigendian; // @todo ?
  output.is_dense = input.is_dense;

  output.data.resize(input.width * input.height * input.point_step);
  // Copy the data points
  memcpy(&output.data[0], &input.data[0],
         input.width * input.height * input.point_step);

  return;
}

// Sets the global variables xOffset, yOffset, zOffset, indexOffset;
static void find_offsets(const sensor_msgs::PointCloud2 & pc)
{
  // find x,y,z,index offset
  for (unsigned int i = 0; i < pc.fields.size(); i++)
  {
    if (pc.fields[i].name.compare("z") == 0)
    {
      zOffset = pc.fields[i].offset;
    }
    else if (pc.fields[i].name.compare("x") == 0)
    {
      xOffset = pc.fields[i].offset;
    }
    else if (pc.fields[i].name.compare("y") == 0)
    {
      yOffset = pc.fields[i].offset;
    }
    else if (pc.fields[i].name.compare("rgb") == 0)
    {
      rgbOffset = pc.fields[i].offset;
    }
    else if (pc.fields[i].name.compare("index") == 0)
    {
      indexOffset = pc.fields[i].offset;
    }
  }
}

// Edits data in-place.  
// Removes points indexed by ind
static void point_remove_by_index(sensor_msgs::PointCloud2 & pc,
                                  pcl::PointIndices ind)
{
  unsigned int nPoints = 0, j = 0;
  for (unsigned int i = 0; i < pc.width * pc.height; i++)
  {
    if (j < ind.indices.size() && i == ind.indices[j])
    {
      j++;
    }
    else if (nPoints != i)
    {
      memcpy(&pc.data[nPoints * pc.point_step],
             &pc.data[i * pc.point_step], pc.point_step);
      nPoints++;
    }
  }
  pc.width = nPoints;
  pc.height = 1;
  pc.is_dense = 0;
  pc.row_step = nPoints * pc.point_step;
  pc.data.resize(nPoints * pc.point_step);
}

// Edits data in-place.  
// Retrieves points indexed by ind
static void point_retrieve_by_index(sensor_msgs::PointCloud2 & pc,
                                    pcl::PointIndices ind)
{
  printf("indices size: %d\n", ind.indices.size());
  // int nPoints = 0;
  for (unsigned int i = 0; i < ind.indices.size(); i++)
  {
    memcpy(&pc.data[i * pc.point_step],
           &pc.data[ind.indices[i] * pc.point_step], pc.point_step);
  }
  /* 
     for (unsigned int i=0;i<pc.width*pc.height;i++) { if (i ==
     ind.indices[nPoints] && nPoints != i) { memcpy(&pc.data[nPoints *
     pc.point_step], &pc.data[i * pc.point_step], pc.point_step); nPoints ++;
     if (nPoints >= ind.indices.size()) break; } } */
  pc.width = ind.indices.size();
  pc.height = 1;
  pc.is_dense = 0;
  pc.row_step = pc.width * pc.point_step;
  pc.data.resize(pc.width * pc.point_step);
}

// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/** \brief Clones and adds an Index field to the point cloud message
 * \param input the message in the sensor_msgs::PointCloud2 format
 * \param output the resultant message in the sensor_msgs::PointCloud2 format
 */
bool
cloneAndAddIndexField(const sensor_msgs::PointCloud2 & input,
                      sensor_msgs::PointCloud2 & output)
{
  if (input.width * input.height < RESOLUTION_X * RESOLUTION_Y)
  {
    ROS_ERROR("ERROR: Incoming point cloud (size: %d) is not dense!",
              input.width * input.height);
    return false;
  }
  output.header = input.header;

  output.width = input.width;
  output.height = input.height;

  output.fields.resize(1 + input.fields.size());  // Extra field to hold
  // index
  // Copy all fields
  for (size_t d = 0; d < input.fields.size(); ++d)
  {
    output.fields[d].name = input.fields[d].name;
    output.fields[d].offset = input.fields[d].offset;
    output.fields[d].datatype = input.fields[d].datatype;
    output.fields[d].count = input.fields[d].count;
  }
  // Set up last field as index
  output.fields[input.fields.size()].name = "index";
  output.fields[input.fields.size()].offset = input.point_step;
  output.fields[input.fields.size()].datatype =
    sensor_msgs::PointField::UINT32;;
  output.fields[input.fields.size()].count = 1;
  output.point_step = input.point_step + 4; // add 4 bytes for index field
  output.row_step = output.point_step * output.width;

  output.is_bigendian = input.is_bigendian; // @todo ?
  output.is_dense = input.is_dense;

  output.data.resize(input.width * input.height * output.point_step);
  // Copy the data points
  for (size_t cp = 0; cp < input.width * input.height; ++cp)
  {
    memcpy(&output.data[cp * output.point_step],
           &input.data[cp * input.point_step], input.point_step);
    // Not sure if this is how to do this.. need to test.
    unsigned int *v =
      (unsigned int
       *)(&output.data[cp * output.point_step + input.point_step]);
    *v = cp;
  }

  return (true);
}


// Tries to find the set of points in pc1 but not in pc2.
// pc2 must be a strict subset of pc1
static void point_cloud_difference(sensor_msgs::PointCloud2 & pc1,
                                   sensor_msgs::PointCloud2 & pc2,
                                   sensor_msgs::PointCloud2 & output)
{
  output.header = pc1.header;
  output.width = pc1.width - pc2.width;
  output.height = pc1.height;
  output.fields.resize(pc1.fields.size());
  // Copy all fields
  for (size_t d = 0; d < pc1.fields.size(); ++d)
  {
    output.fields[d].name = pc1.fields[d].name;
    output.fields[d].offset = pc1.fields[d].offset;
    output.fields[d].datatype = pc1.fields[d].datatype;
    output.fields[d].count = pc1.fields[d].count;
  }
  output.point_step = pc1.point_step; // add 4 bytes for index field
  output.row_step = output.point_step * output.width;

  output.is_bigendian = pc1.is_bigendian; // @todo ?
  output.is_dense = pc1.is_dense;

  output.data.resize(output.width * output.height * output.point_step);
  find_offsets(pc1);

  unsigned int nPoints = 0;
  unsigned int a = 0, b = 0;
  while (nPoints < output.width)
  {
    if (a >= pc1.width)
    {
      printf("ERROR: Difference finding failed\n");
      return;
    }
    if (b >= pc2.width)
    {                           // done with pc2.  copy rest of pc1
      // printf("DONE with pc2\n");
      // printf("%d,%d\n",pc1.width-a,output.width-nPoints);
      memcpy(&output.data[nPoints * output.point_step],
             &pc1.data[a * pc1.point_step], (pc1.width - a) * pc1.point_step);
      nPoints += (pc1.width - a);
      a = pc1.width;
      // printf("DONE with pc1\n");
      return;
    }
    else
    {
      unsigned int *i1 =
        (unsigned int *)(&pc1.data[a * pc1.point_step + indexOffset]);
      unsigned int *i2 =
        (unsigned int *)(&pc2.data[b * pc2.point_step + indexOffset]);
      if (*i1 == *i2)
      {
        a++;
        b++;
      }
      else
      {
        memcpy(&output.data[nPoints * output.point_step],
               &pc1.data[a * pc1.point_step], pc1.point_step);
        a++;
        nPoints++;
      }
    }
  }
  /* while (!(a>=pc1.width || b>=pc2.width)) { unsigned int* i1 = (unsigned
     int*)(&pc1.data[a*pc1.point_step+indexOffset]); unsigned int* i2 =
     (unsigned int*)(&pc2.data[b*pc2.point_step+indexOffset]); if (*i1 == *i2) 
     { a++; b++; } else { printf("ERROR: Points remain that are different!");
     break; a++; nPoints++; } } */
}

// Edits data in-place.
// Removes statistical outliers
static void remove_statistical_outliers(sensor_msgs::PointCloud2 & pc)
{
  pcl::StatisticalOutlierRemoval < sensor_msgs::PointCloud2 > sor;
  // write_point_cloud("pc1.txt",pc);
  sor.setInputCloud(boost::make_shared < sensor_msgs::PointCloud2 > (pc));
  sor.setMeanK(outlier_mean_k);
  sor.setStddevMulThresh(outlier_StddevMulThresh);
  sensor_msgs::PointCloud2 pc2;
  sor.filter(pc2);
  // printf("Done filter\n");
  // write_point_cloud("pc2.txt",pc);
  sensor_msgs::PointCloud2 pc3;
  point_cloud_difference(pc, pc2, pc3);
  // printf("Done difference\n");
  // write_point_cloud("pc3.txt",pc3);
  chatter_pub3.publish(pc3);
  // printf("Done publish\n");
  clone_PointCloud2(pc2, pc);
  // printf("Done clone\n");
  // pc = pc2;
  // exit (0);
}

/*static bool getAngles()
{
  if (!client.call(anglesRequest))
  {
    ROS_ERROR("Failed to call service adept_server");
    return false;
  }
  else
  {
    ROS_INFO("Joint angles: %f %f %f %f %f %f\n",
             (float)anglesRequest.response.j1,
             (float)anglesRequest.response.j2,
             (float)anglesRequest.response.j3,
             (float)anglesRequest.response.j4,
             (float)anglesRequest.response.j5,
             (float)anglesRequest.response.j6);
    return true;
  }
}*/

/*
static bool getTfMatrix()
{
  tfMatRequest.request.j1 = (float)anglesRequest.response.j1;
  tfMatRequest.request.j2 = (float)anglesRequest.response.j2;
  tfMatRequest.request.j3 = (float)anglesRequest.response.j3;
  tfMatRequest.request.j4 = (float)anglesRequest.response.j4;
  if (!client2.call(tfMatRequest))
  {
    ROS_ERROR("Failed to call service AnglesRequest");
    return false;
  }
  else
  {
    ROS_INFO("Transformation matrix:\n%f %f %f %f\n%f %f %f %f\n%f %f %f %f\n",
             (float)tfMatRequest.response.result[0],
             (float)tfMatRequest.response.result[1],
             (float)tfMatRequest.response.result[2],
             (float)tfMatRequest.response.result[3],
             (float)tfMatRequest.response.result[4],
             (float)tfMatRequest.response.result[5],
             (float)tfMatRequest.response.result[6],
             (float)tfMatRequest.response.result[7],
             (float)tfMatRequest.response.result[8],
             (float)tfMatRequest.response.result[9],
             (float)tfMatRequest.response.result[10],
             (float)tfMatRequest.response.result[11]);
    return true;
  }
}*/

/*static void transform_to_global_frame(sensor_msgs::PointCloud2 & in,
                                      sensor_msgs::PointCloud2 & out)
{
  if (!getAngles() || !getTfMatrix())
  {
    return;
  }
  clone_PointCloud2(in, out);
  find_offsets(out);
  // Transform each point
  for (unsigned int i = 0; i < out.width * out.height; i++)
  {
    float *x = (float *)(&out.data[i * out.point_step + xOffset]);
    float *y = (float *)(&out.data[i * out.point_step + yOffset]);
    float *z = (float *)(&out.data[i * out.point_step + zOffset]);
    float xx = *x, yy = *y, zz = *z;
    *x = ((float)tfMatRequest.response.result[0]) * xx +
      ((float)tfMatRequest.response.result[1]) * yy +
      ((float)tfMatRequest.response.result[2]) * zz +
      ((float)tfMatRequest.response.result[3]);
    *y =
      ((float)tfMatRequest.response.result[4]) * xx +
      ((float)tfMatRequest.response.result[5]) * yy +
      ((float)tfMatRequest.response.result[6]) * zz +
      ((float)tfMatRequest.response.result[7]);
    *z =
      ((float)tfMatRequest.response.result[8]) * xx +
      ((float)tfMatRequest.response.result[9]) * yy +
      ((float)tfMatRequest.response.result[10]) * zz +
      ((float)tfMatRequest.response.result[11]);
  }
}*/

// vector<point> pts = new vector<point>();

// Need to enable arbitrary conversion given joint angles
/* 
   static void transform_to_global_frame(vector<geometry_msgs::Point32> &pcd)
   { double trans[3][4] = {{48.19820571, -242.16136401, 37.8146079,
   407.39378972}, {-241.56135225, -39.0242279, 47.94096509, -156.21965005}, {
   -42.95519906, -44.66765686, -241.58229251, 767.63208745}};

   for (int i=0; i<pcd.size(); i++) { double x = pcd[i].x, y = pcd[i].y, z =
   pcd[i].z; pcd[i].x = trans[0][0]*x +
   trans[0][1]*y+trans[0][2]*z+trans[0][3]; pcd[i].y = trans[1][0]*x +
   trans[1][1]*y+trans[1][2]*z+trans[1][3]; pcd[i].z = trans[2][0]*x +
   trans[2][1]*y+trans[2][2]*z+trans[2][3]; } } */

/* 
   // generates a subsample of points, each drawn with probability p static
   vector<point> subsample(sensor_msgs::PointCloud2 &pc, float p) {
   find_offsets(pc); srand( time(NULL) ); vector<point> pts = new
   vector<point>(); // Only selects points with probability p for (unsigned
   int i=0;i<pc.width*pc.height;i++) { float f = (float)(rand()); f =
   f/((float)RAND_MAX); if (f < p) { float* x =
   (float*)(&pc.data[i*pc.point_step+xOffset]); float* y =
   (float*)(&pc.data[i*pc.point_step+yOffset]); float* z =
   (float*)(&pc.data[i*pc.point_step+zOffset]); unsigned int* index =
   (unsigned int*)(&pc.data[i*pc.point_step+indexOffset]); point pt; pt.x = x; 
   pt.y = *y; pt.z = *z; pt.index = *index; pts.push_back(pt); } } } */

// Edits data in-place.  
// Removes points with ax + by + cz + d < 0
static void plane_filter(sensor_msgs::PointCloud2 & pc, plane p)
{
  find_offsets(pc);
  // printf("plane: %f %f %f %f\n", p.a, p.b, p.c, p.d);
  // printf("offsets: %d %d %d\n", xOffset, yOffset, zOffset);
  // Point filter
  int nPoints = 0;
  for (unsigned int i = 0; i < pc.width * pc.height; i++)
  {
    float *x = (float *)(&pc.data[i * pc.point_step + xOffset]);
    float *y = (float *)(&pc.data[i * pc.point_step + yOffset]);
    float *z = (float *)(&pc.data[i * pc.point_step + zOffset]);
    if (p.a * (*x) + p.b * (*y) + p.c * (*z) + p.d >= 0)
    {
      memcpy(&pc.data[nPoints * pc.point_step],
             &pc.data[i * pc.point_step], pc.point_step);
      nPoints++;
    }
  }
  pc.width = nPoints;
  pc.height = 1;
  pc.is_dense = 0;
  pc.row_step = nPoints * pc.point_step;
  pc.data.resize(nPoints * pc.point_step);
}

// Edits data in-place
// Removes the largest "horizontal" plane in the point cloud
static void plane_removal(sensor_msgs::PointCloud2 & pc)
{
  // for (int i=0;i<pc.fields.size();i++)
  // cout << pc.fields[i].name << " " << pc.fields[i].offset << endl;
  // Set up the inputs
  pcl::PointCloud < pcl::PointXYZ > cloud;
  pcl::fromROSMsg(pc, cloud);

  // -----------------------Appliying the segmentation
  // algorithm------------------------- 
  pcl::ModelCoefficients coefficients;
  pcl::PointIndices inliers;
  // Create the segmentation object
  pcl::SACSegmentation < pcl::PointXYZ > seg;
  // Optional
  seg.setOptimizeCoefficients(true);
  // Mandatory
  seg.setModelType(pcl::SACMODEL_PLANE);
  seg.setMethodType(pcl::SAC_RANSAC);
  seg.setDistanceThreshold(ransac_tol);
  seg.setMaxIterations(ransac_iter);
  seg.setInputCloud(boost::make_shared < pcl::PointCloud < pcl::PointXYZ >
                    >(cloud));
  time_t tstart, tend;

  printf("Start segmentation\n");
  tstart = time(NULL);
  seg.segment(inliers, coefficients);
  tend = time(NULL);
  ROS_INFO("Segmentation Time elapsed %f", difftime(tend, tstart));

  if (inliers.indices.size() == 0)
  {
    ROS_ERROR("Could not estimate a planar model for the given dataset.");
    return;
  }
  ROS_INFO("Done segmentation of initial cloud");
  ROS_INFO("No. of Inliers :  %d", inliers.indices.size());
  ROS_INFO("Model coefficients : %f, %f, %f, %f", coefficients.values[0],
           coefficients.values[1], coefficients.values[2],
           coefficients.values[3]);

  sensor_msgs::PointCloud2 plane;
  clone_PointCloud2(pc, plane);
  printf("Plane size: %d %d\n", plane.width, plane.height);
  // Remove these points
  point_retrieve_by_index(plane, inliers);
  point_remove_by_index(pc, inliers);
  printf("Plane size: %d %d\n", plane.width, plane.height);
  chatter_pub2.publish(plane);
}

// Edits data in-place.
// OLD: Removes points with z val < maxZ - eps
// NEW: Removes NaN points
static void clean_missing_point(sensor_msgs::PointCloud2 & pc)
{
  /* Old way of cleaning
  double maxz = -1e10, eps = 1e-6;
  find_offsets(pc);

  // get maximum z
  for (unsigned int i = 0; i < pc.width * pc.height; i++)
  {
    float *p = (float *)(&pc.data[i * pc.point_step + zOffset]);
    if (*p > maxz)
      maxz = *p;
  }
  // printf("naxz: %f\n",maxz);

  // Remove all points above maxz-eps
  plane pl;
  pl.a = 0;
  pl.b = 0;
  pl.c = -1.0;
  pl.d = (maxz - eps);
  plane_filter(pc, pl);
  */
  find_offsets(pc);
  int nPoints = 0;
  for (unsigned int i = 0; i < pc.width * pc.height; i++)
  {
    float *x = (float *)(&pc.data[i * pc.point_step + xOffset]);
    float *y = (float *)(&pc.data[i * pc.point_step + yOffset]);
    float *z = (float *)(&pc.data[i * pc.point_step + zOffset]);
    if (!(isnan(*x) || isnan(*y) || isnan(*z)))
    {
      memcpy(&pc.data[nPoints * pc.point_step],
             &pc.data[i * pc.point_step], pc.point_step);
      nPoints++;
    }
  }
  pc.width = nPoints;
  pc.height = 1;
  pc.is_dense = 0;
  pc.row_step = nPoints * pc.point_step;
  pc.data.resize(nPoints * pc.point_step);

  /* 
     int nPoints = 0; //remove all points beyond this threshold for (unsigned
     int i=0; i<pc.width*pc.height; i++) { float* p =
     (float*)(&pc.data[i*pc.point_step+zOffset]); if (*p < maxz-eps) {
     memcpy(&pc.data[nPoints * pc.point_step], &pc.data[i * pc.point_step],
     pc.point_step); nPoints ++; } } pc.width = nPoints; pc.height = 1;
     pc.is_dense = 0; pc.row_step = nPoints * pc.point_step;
     pc.data.resize(nPoints*pc.point_step); */
}

// For testing use only
static void print_first_10(const sensor_msgs::PointCloud2 & pc)
{
  find_offsets(pc);
  // Print first ten points
  for (unsigned int i = 0; i < 10 && i < pc.width * pc.height; i++)
  {
    float *x = (float *)(&pc.data[i * pc.point_step + xOffset]);
    float *y = (float *)(&pc.data[i * pc.point_step + yOffset]);
    float *z = (float *)(&pc.data[i * pc.point_step + zOffset]);
    unsigned int *index =
      (unsigned int *)(&pc.data[i * pc.point_step + indexOffset]);
    printf("%f, %f, %f, %d\n", *x, *y, *z, *index);
  }
}

/*
// Writes the joint angles and the transformation matrix to a file
static void writeAngles(const char *fname, bool binary_mode)
{
  if (!getAngles() || !getTfMatrix())
  {
    return;
  }
  ofstream file;
  //file.open(fname, ios::out | (binary_mode ? ios::binary : ios::out));
  file.open(fname, ios::out);
  if (binary_mode)
  {
    file << (float)anglesRequest.response.j1 << " "
      << (float)anglesRequest.response.j2 << " "
      << (float)anglesRequest.response.j3 << " "
      << (float)anglesRequest.response.j4 << " "
      << (float)anglesRequest.response.j5 << " "
      << (float)anglesRequest.response.j6 << endl;
    for (unsigned int i = 0; i < 12; i++)
    {
      file << (float)tfMatRequest.response.result[i] << " ";
    }
    file << (float)tfMatRequest.response.result[11] << endl;
  }
  else
  {                             // Ascii mode
    file.precision(write_ascii_precision);
    file << (float)anglesRequest.response.j1 << " "
      << (float)anglesRequest.response.j2 << " "
      << (float)anglesRequest.response.j3 << " "
      << (float)anglesRequest.response.j4 << " "
      << (float)anglesRequest.response.j5 << " "
      << (float)anglesRequest.response.j6 << endl;
    for (unsigned int i = 0; i < 11; i++)
    {
      file << (float)tfMatRequest.response.result[i] << " ";
    }
    file << (float)tfMatRequest.response.result[11] << endl;
  }
  return;
}
*/

// Simply uses pcl::io library to save the point cloud to a file
static void write_point_cloud(const char *fname,
                              const sensor_msgs::PointCloud2 & pc,
                              const sensor_msgs::PointCloud2 & pcraw,
                              bool binary_mode)
{
  // pcl::io::savePCDFile(fname,pc,binary_mode);
  pcl::PointCloud < pcl::PointXYZRGB > cloud;
  pcl::fromROSMsg(pc, cloud);
  pcl::PointCloud < my_ns::MyPoint > myCloud;
  pcl::fromROSMsg(pc, myCloud);
  if (binary_mode)
  {
    //pcl::io::savePCDFile(fname, pc, Eigen::Vector4f::Zero(), Eigen::Quaternionf::Identity(), true);
    //pcl::io::savePCDFile(fname, pc, true);
    pcl::io::savePCDFile(fname, myCloud, true);
  }
  else
  {
    //pcl::io::savePCDFile(fname, pc, Eigen::Vector4f::Zero(), Eigen::Quaternionf::Identity(), false);
    pcl::io::savePCDFile(fname, myCloud);
    //pcl::io::savePCDFile(fname, pc, true);
    //pcl::io::savePCDFile(fname, cloud);
  }

  char name[50];
  IplImage *rawImage = bridge.imgMsgToCv(image, "bgr8");
  sprintf(name, "/tmp/%s%04dc.png", write_file_prefix.c_str(), write_file_num);
  cvSaveImage(name, rawImage);

  if (reconstruct)
  {
    pcl::PointCloud < pcl::PointXYZRGB > cloudorig;
    pcl::fromROSMsg(pcraw, cloudorig);
    IplImage *resultImage =
      cvCreateImage(cvSize(RESOLUTION_X, RESOLUTION_Y), IPL_DEPTH_8U, 3);


    for (int row = 0; row < resultImage->height; row++)
    {
      unsigned char *ptr =
        (unsigned char *)(resultImage->imageData +
                          row * resultImage->widthStep);
      for (int col = 0; col < resultImage->width; col++)
      {
        // Image is in BGR order
        int rgb =
          *reinterpret_cast <
          int *>(&(cloudorig.points[row * cloudorig.width + col].rgb));
        ptr[3 * col] = (uchar) (rgb & 0xff);
        ptr[3 * col + 1] = (uchar) ((rgb >> 8) & 0xff);
        ptr[3 * col + 2] = (uchar) ((rgb >> 16) & 0xff);
      }
    }

    sprintf(name, "/tmp/%s%04dr.png", write_file_prefix.c_str(), write_file_num);
    cvSaveImage(name, resultImage);
    cvReleaseImage(&resultImage);
  }
}


static void image_callback(const sensor_msgs::ImageConstPtr & msg)
{
  boost::lock_guard<boost::mutex> guard(global_mutex);
  image = msg;
}

static void depth_callback(const sensor_msgs::ImageConstPtr & msg)
{
  boost::lock_guard<boost::mutex> guard(global_mutex);
  depth_image = msg;
}

// Need to rewrite this to do what we want
static void point_cloud_callback(const sensor_msgs::PointCloud2::
                                 ConstPtr & data)
{
  boost::lock_guard<boost::mutex> guard(global_mutex);
  if (!run)
    return;

  printf("size: %d, width: %d, height: %d\n", data->width * data->height,
         data->width, data->height);

  // clones the msg and adds the index field
  if (!cloneAndAddIndexField((*data), pcd))
    return;
  // discard all missing point and store the rest in pcd
  if (clean_missing_points)
  {
    clean_missing_point(pcd);
    printf("Cleaned. size: %d, width: %d, height: %d\n",
           pcd.width * pcd.height, pcd.width, pcd.height);
  }
  // Remove the largest plane in the point cloud
  if (remove_plane)
  {
    plane_removal(pcd);
    printf("After plane removal, size: %d, width: %d, height: %d\n",
           pcd.width * pcd.height, pcd.width, pcd.height);
  }
  if (remove_outlier)
  {
    remove_statistical_outliers(pcd);
    printf("After statistical outlier removal, size: %d\n",
           pcd.width * pcd.height);
  }
  sensor_msgs::PointCloud2 transformedPC;
  /*if (transform_to_global)
  {
    transform_to_global_frame(pcd, transformedPC);
  }
  if (remove_user_plane)
  {
    plane_filter(transform_to_global ? transformedPC : pcd, user_plane);
    printf("After user-plane removal, size: %d\n", pcd.width * pcd.height);
  }*/
  if (estimate_normals)
  {
    ROS_ERROR("Normal estimation disabled");
    estimate_normals = false;
  }
  if (debug)
  {
    printf("PCD:\n");
    print_first_10(pcd);
    printf("TRANSFORMED:\n");
    print_first_10(transformedPC);
  }  
  chatter_pub.publish(pcd);
  /*if (transform_to_global)
  {
    chatter_pub4.publish(transformedPC);
  }*/
  if (write_next)
  {

    char name[50];
    // Write the point cloud to a file
    sprintf(name, "/tmp/%s%04d.txt", write_file_prefix.c_str(), write_file_num);
    //write_point_cloud(name, transform_to_global ? transformedPC : pcd, (*data), write_binary); //david commented
    write_point_cloud(name, pcd, (*data), write_binary); //david
    // Also write the angles and transformation matrix if available
    sprintf(name, "/tmp/%s%04d_angles.txt", write_file_prefix.c_str(),
            write_file_num);
    //writeAngles(name, write_binary);
    // Save depth image
    IplImage *img = g_bridge.imgMsgToCv(depth_image,"32FC1");
    if (img) {
      double mi=1e100,ma=-1e100;
      int width = img->width;
      int height = img->height;
      int step = img->widthStep;
      //printf ("%d %d %d\n",width,height,step);
      uchar *p = (uchar*)img->imageData;
      float *d;
      for(int i=0;i<height;i++)
      {
	d = (float*)p;
	for(int j=0;j<width;j++)
	{
	  if (cvIsNaN(*d) || cvIsInf(*d)) {
	    *d = 0.0;
	  }
	  if (*d > ma)
	    ma = *d;
	  if (*d < mi)
	    mi = *d;
	  d++;
	}
	p+=step;//make your pointer points to the next row
      }
      //printf("%g %g\n",mi,ma);
      double scale = 255.0/(ma-mi);
      double shift = -(mi*scale);
      cvConvertScale(img,img,scale,shift);
      char filename[200];
      sprintf(filename,"/tmp/frame%04d.png",write_file_num);
      //cout << "Saving image ..."; flush(cout);
      cvSaveImage(filename, img);
      //cout << " releasing ..."; flush(cout);
      //cvReleaseImage(&img);
      //cout << " done" << endl; flush(cout);
    }
    //write_file_num++;
    write_next = false;
    // Update the GUI
    //cout << "Trying to update config..."; flush(cout);
    point_cloud_utils::utilConfig config;
    //config.run = run;
    //config.ransac_tol = ransac_tol;
    //config.ransac_iter = ransac_iter;
    //config.debug = debug;
    //config.remove_plane = remove_plane;
    //config.remove_outlier = remove_outlier;
    config.write_next = write_next;
    //config.write_binary = write_binary;
    //config.write_depth_map = write_depth_map;
    //config.write_ascii_precision = write_ascii_precision;
    //config.write_file_num = write_file_num;
    //config.write_file_prefix = write_file_prefix;
    //config.outlier_mean_k = outlier_mean_k;
    //config.outlier_StddevMulThresh = outlier_StddevMulThresh;
    //config.user_plane_a = user_plane.a;
    //config.user_plane_b = user_plane.b;
    //config.user_plane_c = user_plane.c;
    //config.user_plane_d = user_plane.d;
    //config.remove_user_plane = remove_user_plane;
    //config.transform_to_global = transform_to_global;
    //config.estimate_normals = estimate_normals;
    //config.normals_k_neighbors = normals_k_neighbors;
    //config.clean_missing_points = clean_missing_points;
    srv->updateConfig(config);
    //cout << " done" << endl; flush(cout);

    //david
    printf("Start feature calculation\n");
    int i = system("cd `rospack find svmlight`; ./runnew.sh");
    printf("Finished feature calculation\n");
  }
}

// * Dynamic reconfigure callback */
void reconfig(point_cloud_utils::utilConfig & config, uint32_t level)
{
  boost::lock_guard<boost::mutex> guard(global_mutex);
  /*ROS_INFO_STREAM("Reconfigure request: run " << config.run
                  << ", debug " << config.debug
                  << ", remove_plane " << config.remove_plane
                  << ", ransac_tol " << config.ransac_tol
                  << ", ransac_iter " << config.ransac_iter
                  << ", write_binary " << config.write_binary
                  << ", write_ascii_precision " <<
                  config.write_ascii_precision << ", write_next " <<
                  config.write_next << ", write_file_num " <<
                  config.write_file_num << ", write_file_prefix " <<
                  config.write_file_prefix);*/


  // ROS_INFO_STREAM("reconfigure level = " << level);

  // Update parameters
  run = true;			//config.run;
  ransac_tol = 0.025; 	//config.ransac_tol;
  ransac_iter = 20; 	//config.ransac_iter;
  debug = false;		// config.debug;
  remove_plane = false;	//config.remove_plane;
  remove_outlier = false; //config.remove_outlier;
  write_next = config.write_next;
  write_binary = false;	//config.write_binary;
  write_depth_map = false; //config.write_depth_map;
  write_ascii_precision = 12;	//config.write_ascii_precision;
  write_file_num = 1;			//config.write_file_num;
  write_file_prefix = "scene";	//config.write_file_prefix;
  outlier_mean_k = 50;	//config.outlier_mean_k;
  outlier_StddevMulThresh = 1;	//config.outlier_StddevMulThresh;
  //user_plane.a = config.user_plane_a;
  //user_plane.b = config.user_plane_b;
  //user_plane.c = config.user_plane_c;
  //user_plane.d = config.user_plane_d;
  remove_user_plane = false;	//config.remove_user_plane;
  //transform_to_global = config.transform_to_global;
  estimate_normals = false;	//config.estimate_normals;
  normals_k_neighbors = 20;	// config.normals_k_neighbors;
  clean_missing_points = true;	//config.clean_missing_points;

  ROS_DEBUG_STREAM("Reconfigured td: write_next " << config.write_next << ", write_file_num " <<  write_file_num );
}


int main(int argc, char *argv[])
{
  /* if (argc <= 1) { ransac_tol = 0.025; } else { ransac_tol = atof(argv[1]); 
     } */
  // setup subscriber
  ros::init(argc, argv, "util");
  ros::NodeHandle nd;
  ros::Subscriber pcdSub;
  ros::Subscriber imgSub;
  ros::Subscriber depthSub;
  if (argc < 2)
  {
    RESOLUTION_X = 640;
    RESOLUTION_Y = 480;
    pcdSub =
      // nd.subscribe("/kinect/rgb/points2", 1, point_cloud_callback);
      //nd.subscribe("/camera/depth/points2", 1, point_cloud_callback);  //commented david
      nd.subscribe("/camera/rgb/points", 1, point_cloud_callback);  //david
    imgSub =
      // nd.subscribe("/kinect/rgb/image_rect_color", 1, image_callback);
      nd.subscribe("/camera/rgb/image_color", 1, image_callback);
    depthSub =
      // nd.subscribe("/kinect/rgb/image_rect_color", 1, image_callback);
      nd.subscribe("/camera/depth/image", 1, depth_callback);
    reconstruct = true;
  }

  chatter_pub =
    (nd.advertise < sensor_msgs::PointCloud2 >
     ("/point_cloud_utils/cleaned_point_cloud", 1));
  chatter_pub2 =
    (nd.advertise < sensor_msgs::PointCloud2 >
     ("/point_cloud_utils/plane", 1));
  chatter_pub3 =
    (nd.advertise < sensor_msgs::PointCloud2 >
     ("/point_cloud_utils/outliers", 1));
  chatter_pub4 =
    (nd.advertise < sensor_msgs::PointCloud2 >
     ("/point_cloud_utils/transformed_pc", 1));
  chatter_pub5 =
    (nd.advertise < sensor_msgs::PointCloud2 >
     ("/point_cloud_utils/normals", 1));
  //client = nd.serviceClient < adept_server::AnglesRequest > ("adept_ang_req");
  //client2 = nd.serviceClient < arm_geometry::TFMatRequest > ("tf_mat");

  srv = new dynamic_reconfigure::Server < point_cloud_utils::utilConfig > ();
  dynamic_reconfigure::Server <
    point_cloud_utils::utilConfig >::CallbackType f =
    boost::bind(&reconfig, _1, _2);
  srv->setCallback(f);
  // clean up
  ros::spin();

  return 0;
}