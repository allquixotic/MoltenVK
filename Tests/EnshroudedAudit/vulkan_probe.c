/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "volk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "compute.comp.h"
#include "count.vert.h"

#define CHECK(call) do { VkResult result=(call); if(result!=VK_SUCCESS){fprintf(stderr,"%s failed: %d at %d\n",#call,result,__LINE__);exit(2);} } while(0)
static VkDevice dev;
static VkPhysicalDevice gpu;
typedef struct Buffer { VkBuffer handle; VkDeviceMemory memory; void *mapped; } Buffer;
static Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer out={0}; VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
    CHECK(vkCreateBuffer(dev,&bi,NULL,&out.handle));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev,out.handle,&req);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(gpu,&mp);
    uint32_t type=UINT32_MAX;
    for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((req.memoryTypeBits&(1u<<i)) && (mp.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
    if(type==UINT32_MAX){fprintf(stderr,"No coherent host memory\n");exit(2);}
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
    CHECK(vkAllocateMemory(dev,&ai,NULL,&out.memory)); CHECK(vkBindBufferMemory(dev,out.handle,out.memory,0)); CHECK(vkMapMemory(dev,out.memory,0,VK_WHOLE_SIZE,0,&out.mapped)); return out;
}
static void free_buffer(Buffer b) { vkUnmapMemory(dev,b.memory); vkDestroyBuffer(dev,b.handle,NULL); vkFreeMemory(dev,b.memory,NULL); }
static VkShaderModule shader(const uint32_t *code,size_t size) { VkShaderModule s; VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=size,.pCode=code}; CHECK(vkCreateShaderModule(dev,&ci,NULL,&s));return s; }
int main(int argc,char**argv) {
    setvbuf(stdout,NULL,_IONBF,0);
#ifdef _WIN32
    HMODULE lib=LoadLibraryA("vulkan-1.dll");
    PFN_vkGetInstanceProcAddr get=(PFN_vkGetInstanceProcAddr)GetProcAddress(lib,"vkGetInstanceProcAddr");
#else
    if(argc<2) return 2;
    void*lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if(!lib){fprintf(stderr,"%s\n",dlerror());return 2;}
    PFN_vkGetInstanceProcAddr get=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vkGetInstanceProcAddr");
#endif
    if(!get)return 2; volkInitializeCustom(get);
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Enshrouded runtime audit probe",.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app}; VkInstance instance;
    CHECK(vkCreateInstance(&ici,NULL,&instance)); volkLoadInstance(instance);
    uint32_t n=1; CHECK(vkEnumeratePhysicalDevices(instance,&n,&gpu));
    VkPhysicalDeviceDriverProperties driver={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&driver}; vkGetPhysicalDeviceProperties2(gpu,&props);
    printf("device=%s driver=%s info=%s\n",props.properties.deviceName,driver.driverName,driver.driverInfo);
    n=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,NULL);VkQueueFamilyProperties*qps=calloc(n,sizeof(*qps));vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,qps);
    uint32_t family=UINT32_MAX;for(uint32_t i=0;i<n;i++)if((qps[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){family=i;break;} free(qps); if(family==UINT32_MAX)return 2;
    VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext=&f13};
    VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f12};vkGetPhysicalDeviceFeatures2(gpu,&features);
    printf("drawIndirectCount=%u dynamicRendering=%u vertexStores=%u\n",f12.drawIndirectCount,f13.dynamicRendering,features.features.vertexPipelineStoresAndAtomics);
    if(!f12.drawIndirectCount||!f13.dynamicRendering||!features.features.vertexPipelineStoresAndAtomics)return 2;
    f12=(VkPhysicalDeviceVulkan12Features){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext=&f13,.drawIndirectCount=VK_TRUE};
    f13=(VkPhysicalDeviceVulkan13Features){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.dynamicRendering=VK_TRUE};
    VkPhysicalDeviceFeatures enabled={.multiDrawIndirect=VK_TRUE,.vertexPipelineStoresAndAtomics=VK_TRUE};
    n=0;CHECK(vkEnumerateDeviceExtensionProperties(gpu,NULL,&n,NULL));VkExtensionProperties*eps=calloc(n,sizeof(*eps));CHECK(vkEnumerateDeviceExtensionProperties(gpu,NULL,&n,eps));
    const char*extensions[1];uint32_t extn=0;for(uint32_t i=0;i<n;i++)if(!strcmp(eps[i].extensionName,"VK_KHR_portability_subset"))extensions[extn++]="VK_KHR_portability_subset";free(eps);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f12,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.pEnabledFeatures=&enabled,.enabledExtensionCount=extn,.ppEnabledExtensionNames=extensions};CHECK(vkCreateDevice(gpu,&di,NULL,&dev));volkLoadDevice(dev);
    VkQueue queue;vkGetDeviceQueue(dev,family,0,&queue);
    int indexed=getenv("PROBE_INDEXED")!=NULL;
    Buffer index=buffer(4,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);*(uint32_t*)index.mapped=0;
    Buffer data=buffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT),readback=buffer(256,VK_BUFFER_USAGE_TRANSFER_DST_BIT),indirect=buffer(1024*sizeof(VkDrawIndexedIndirectCommand),VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT),count=buffer(4,VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    memset(data.mapped,0,256);memset(readback.mapped,0,256);*(uint32_t*)count.mapped=256;
    if(indexed){VkDrawIndexedIndirectCommand*draws=indirect.mapped;for(int i=0;i<1024;i++)draws[i]=(VkDrawIndexedIndirectCommand){.indexCount=1,.instanceCount=1};}
    else {VkDrawIndirectCommand*draws=indirect.mapped;for(int i=0;i<1024;i++)draws[i]=(VkDrawIndirectCommand){.vertexCount=1,.instanceCount=1};}
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_VERTEX_BIT};
    VkDescriptorSetLayoutCreateInfo sli={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};VkDescriptorSetLayout sl;CHECK(vkCreateDescriptorSetLayout(dev,&sli,NULL,&sl));
    VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};VkDescriptorPool dp;CHECK(vkCreateDescriptorPool(dev,&dpi,NULL,&dp));
    VkDescriptorSetAllocateInfo sai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=dp,.descriptorSetCount=1,.pSetLayouts=&sl};VkDescriptorSet ds;CHECK(vkAllocateDescriptorSets(dev,&sai,&ds));
    VkDescriptorBufferInfo dbi={data.handle,0,256};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=ds,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&dbi};vkUpdateDescriptorSets(dev,1,&w,0,NULL);
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl};VkPipelineLayout pl;CHECK(vkCreatePipelineLayout(dev,&pli,NULL,&pl));
    VkShaderModule cs=shader(compute_spv,sizeof(compute_spv)),vs=shader(vertex_spv,sizeof(vertex_spv));
    VkComputePipelineCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=cs,.pName="main"},.layout=pl};VkPipeline cp;CHECK(vkCreateComputePipelines(dev,VK_NULL_HANDLE,1,&cpi,NULL,&cp));
    VkCommandPoolCreateInfo cpooli={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};VkCommandPool cpool;CHECK(vkCreateCommandPool(dev,&cpooli,NULL,&cpool));
    VkCommandBufferAllocateInfo cbi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cpool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cb;CHECK(vkAllocateCommandBuffers(dev,&cbi,&cb));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cb,&begin));vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,cp);vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&ds,0,NULL);
    VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
    for(int i=0;i<64;i++){vkCmdDispatch(cb,1,1,1);vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,1,&mb,0,NULL,0,NULL);}
    mb.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&mb,0,NULL,0,NULL);VkBufferCopy copy={0,0,256};vkCmdCopyBuffer(cb,data.handle,readback.handle,1,&copy);mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};CHECK(vkQueueSubmit(queue,1,&si,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(queue));
    for(int i=0;i<64;i++)if(((uint32_t*)readback.mapped)[i]!=64){fprintf(stderr,"COMPUTE BARRIER FAILURE at %d: %u\n",i,((uint32_t*)readback.mapped)[i]);return 3;}printf("compute_barriers_and_deferred_copy=PASS\n");
    VkPipelineShaderStageCreateInfo stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"};VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST};VkViewport viewport={0,0,1,1,0,1};VkRect2D rect={{0,0},{1,1}};VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&rect};VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.rasterizerDiscardEnable=VK_TRUE,.polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineRenderingCreateInfo rendering={.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};VkGraphicsPipelineCreateInfo gi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.pNext=&rendering,.stageCount=1,.pStages=&stage,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.layout=pl};VkPipeline gp;CHECK(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&gi,NULL,&gp));
    CHECK(vkResetCommandBuffer(cb,0));CHECK(vkBeginCommandBuffer(cb,&begin));VkRenderingInfo ri={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,.renderArea=rect,.layerCount=1};vkCmdBeginRendering(cb,&ri);vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,gp);vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,0,1,&ds,0,NULL);if(indexed){vkCmdBindIndexBuffer(cb,index.handle,0,VK_INDEX_TYPE_UINT32);vkCmdDrawIndexedIndirectCount(cb,indirect.handle,0,count.handle,0,1024,sizeof(VkDrawIndexedIndirectCommand));}else{vkCmdDrawIndirectCount(cb,indirect.handle,0,count.handle,0,1024,sizeof(VkDrawIndirectCommand));}vkCmdEndRendering(cb);mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);CHECK(vkEndCommandBuffer(cb));
    for(int i=0;i<6;i++){memset(data.mapped,0,256);CHECK(vkQueueSubmit(queue,1,&si,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(queue));printf("reused_indirect_count_submission_%d=%u expected=256\n",i,*(uint32_t*)data.mapped);}
    int failed=*(uint32_t*)data.mapped!=256;printf("adaptive_cap_final=%s indexed=%d\n",failed?"FAIL":"PASS",indexed);
    CHECK(vkDeviceWaitIdle(dev));vkDestroyPipeline(dev,gp,NULL);vkDestroyPipeline(dev,cp,NULL);vkDestroyShaderModule(dev,vs,NULL);vkDestroyShaderModule(dev,cs,NULL);vkDestroyCommandPool(dev,cpool,NULL);vkDestroyPipelineLayout(dev,pl,NULL);vkDestroyDescriptorPool(dev,dp,NULL);vkDestroyDescriptorSetLayout(dev,sl,NULL);free_buffer(data);free_buffer(readback);free_buffer(indirect);free_buffer(count);free_buffer(index);vkDestroyDevice(dev,NULL);vkDestroyInstance(instance,NULL);return failed?4:0;
}
