#include "DiscreteCollisionDetector.h"
#include <CuMatrix/MatrixOps/CuMatrix.h>
#include <CuMatrix/Geometry/Geometry.h>
#include "../TetMesh/TetMeshFEM.h"

using namespace SP;
using embree::Vec3fa;

#define ABSOLUTE_RELAXIATION 0.f

inline embree::Vec3fa SP::loadVertexPos(TetMeshFEM* pTM, int32_t vId)
{
    return embree::Vec3fa::loadu(pTM->mVertPos.col(vId).data());
}

inline embree::Vec3fa SP::faceNormal(TetMeshFEM* pTM, int32_t faceId)
{
    embree::Vec3fa a = loadVertexPos(pTM, pTM->surfaceFacesTetMeshVIds(0, faceId)),
        b = loadVertexPos(pTM, pTM->surfaceFacesTetMeshVIds(1, faceId)),
        c = loadVertexPos(pTM, pTM->surfaceFacesTetMeshVIds(2, faceId));

    embree::Vec3fa ab = b - a;
    embree::Vec3fa ac = c - a;

    embree::Vec3fa normal = embree::cross(ab, ac);
    normal = embree::normalize(normal);

    return normal;
}

inline embree::Vec3fa faceOrientedArea(TetMeshFEM* pTM, int32_t faceId)
{
    embree::Vec3fa a = loadVertexPos(pTM, pTM->surfaceFacesTetMeshVIds(0, faceId)),
        b = loadVertexPos(pTM, pTM->surfaceFacesTetMeshVIds(1, faceId)),
        c = loadVertexPos(pTM, pTM->surfaceFacesTetMeshVIds(2, faceId));

    embree::Vec3fa ab = b - a;
    embree::Vec3fa ac = c - a;

    embree::Vec3fa orientedArea = embree::cross(ab, ac);

    return orientedArea;
}


Vec3fa SP::closestPointTriangle(Vec3fa const& p, Vec3fa const& a, Vec3fa const& b, Vec3fa const& c,
    Vec3fa& baryCentrics, ClosestPointOnTriangleType& pointType)
{
    const Vec3fa ab = b - a;
    const Vec3fa ac = c - a;
    const Vec3fa ap = p - a;

    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) {
        pointType = ClosestPointOnTriangleType::AtA;
        baryCentrics = Vec3fa(1.f, 0.f, 0.f);

        return a;
    }

    const Vec3fa bp = p - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) {
        pointType = ClosestPointOnTriangleType::AtB;
        baryCentrics = Vec3fa(0.f, 1.f, 0.f);
        return b;
    }

    const Vec3fa cp = p - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) {
        pointType = ClosestPointOnTriangleType::AtC;
        baryCentrics = Vec3fa(0.f, 0.f, 1.f);
        return c;
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
    {
        const float v = d1 / (d1 - d3);
        pointType = ClosestPointOnTriangleType::AtAB;
        baryCentrics = Vec3fa(1.0f - v, v, 0.f);
        return a + v * ab;
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
    {
        const float v = d2 / (d2 - d6);
        pointType = ClosestPointOnTriangleType::AtAC;
        baryCentrics = Vec3fa(1.0f - v, 0.f, v);
        return a + v * ac;
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
    {
        pointType = ClosestPointOnTriangleType::AtBC;
        const float v = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        baryCentrics = Vec3fa(0.f, 1.f - v, v);
        return b + v * (c - b);
    }

    const float denom = 1.f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    pointType = ClosestPointOnTriangleType::AtInterior;
    baryCentrics = Vec3fa(1.f - v - w, v, w);
    return a + v * ab + w * ac;
}

bool tetIntersectionFunc(RTCPointQueryFunctionArguments* args)
{
    CollisionDetectionResult* result = (CollisionDetectionResult*)args->userPtr;


    //the pointer to the mesh that has potential collision
    //TM::Ptr pTM = (*(result->pTetmeshGeoIdToPointerMap))[args->geomID];
    DiscreteCollisionDetector* pDCD = (DiscreteCollisionDetector*)result->pDetector;
    TetMeshFEM* pTMIntersected = pDCD->tMeshPtrs[args->geomID].get();

    assert(args->userPtr);
    const unsigned int geomID = args->geomID;
    const unsigned int primID = args->primID;

    RTCPointQueryContext* context = args->context;

    IdType intersectedTId = primID;
    IdType* tetVIds = pTMIntersected->mTetVIds.data() + (4 * intersectedTId);

    // std::cout << "geomID: " << geomID << "\n";

    //if (result->pTMToCheck != nullptr && result->pTMToCheck != pTM)
    //{
    //    return false;
    //}

    if (!result->handleSelfIntersection && geomID == result->idTMQuery)
    {
        // do not detect the self-intersection in this case
        return false;
    }

    // to avoid false negative of intersection with itself
    if (result->idTMQuery != -1 && geomID == result->idTMQuery) {

        if (result->idVQuery != -1) {
            for (int i = 0; i < 4; i++)
            {
                if (result->idVQuery == tetVIds[i]) {
                    return false;
                }
            }

            //if (result->idVQuery == tetVId1 ||
            //    result->idVQuery == tetVId2 ||
            //    result->idVQuery == tetVId3 ||
            //    result->idVQuery == tetVId3 )
            //{
            //    return false;
            //}
        }
        if (result->idTetQuery != -1) {
            if (result->idTetQuery == intersectedTId)
            {
                return false;
            }
        }
    }

    FloatingType p[3] = { args->query->x, args->query->y, args->query->z };
    if (CuMatrix::tetPointInTet(p, pTMIntersected->mVertPos.data(), tetVIds)) {
        result->intersectedTets.push_back(intersectedTId);
        result->intersectedTMeshIds.push_back(geomID);

    }
    return false;

}

bool closestPointQueryFunc(RTCPointQueryFunctionArguments* args)
{
    ClosestPointQueryResult* result = (ClosestPointQueryResult*)args->userPtr;
    // TetMeshFEM* pTMQuery = result->pDCD->tMeshPtrs[result->idTMQuery].get();
    ++result->numberOfBVHQuery;
    if (result->numberOfBVHQuery > result->pDCD->params.maxNumberOfBVHQuery) {
        result->found = false;
        args->query->radius = 0;
        return true;
    }
                                               
    DiscreteCollisionDetector* pDCD = result->pDCD;

    assert(args->userPtr);
    const unsigned int geomID = args->geomID;
    const unsigned int primID = args->primID;
    TetMeshFEM* pTMSearch = result->pDCD->tMeshPtrs[geomID].get();

    embree::Vec3fa queryPt(args->query->x, args->query->y, args->query->z);
    //PathFinder::CPoint qq = 
    /*
     * Get triangle information in local space
     */

    embree::Vec3ia face(pTMSearch->surfaceFacesTetMeshVIds(0, primID), pTMSearch->surfaceFacesTetMeshVIds(1, primID), pTMSearch->surfaceFacesTetMeshVIds(2, primID));

    embree::Vec3fa a = embree::Vec3fa::loadu(pTMSearch->mVertPos.col(face[0]).data());
    embree::Vec3fa b = embree::Vec3fa::loadu(pTMSearch->mVertPos.col(face[1]).data());
    embree::Vec3fa c = embree::Vec3fa::loadu(pTMSearch->mVertPos.col(face[2]).data());

    ///*
    // * Determine distance to closest point on triangle (implemented in
    // * common/math/closest_point.h), and transform in world space if necessary.
    // */
    ClosestPointOnTriangleType pointType;
    Vec3fa closestPtBarycentrics;
    Vec3fa closestP = SP::closestPointTriangle(queryPt, a, b, c, closestPtBarycentrics, pointType);
    float d = embree::distance(queryPt, closestP);
    // printf_s("Queried triangle with distance: %f\n", d);

    if (geomID == result->idTMQuery)
        // self intersection
    {
        int32_t closestPtId = -1;
        switch (pointType)
        {
        case ClosestPointOnTriangleType::AtA:
            closestPtId = face[0];
            break;
        case ClosestPointOnTriangleType::AtB:
            closestPtId = face[1];
            break;
        case ClosestPointOnTriangleType::AtC:
            closestPtId = face[2];
            break;
        default:
            break;
        }

        if (closestPtId != -1)
        {
            if (closestPtId == result->idVQuery) {
                return false;
            }
        }
    }

    ///*
    // * Store result in userPtr and update the query radius if we found a point
    // * closer to the query position. This is optional but allows for faster
    // * traversal (due to better culling).
    // */
    if (d < args->query->radius)
    {
        bool inFeasibleRegion = false;

        if (result->checkFeasibleRegion)
        {
            inFeasibleRegion = result->pDCD->checkFeasibleRegion(queryPt, pTMSearch, primID, pointType, pDCD->params.feasibleRegionEpsilon);
        }
        else
        {
            inFeasibleRegion = true;
        }
        if (!inFeasibleRegion) {
            return false;
        }
        else if (result->checkTetTraverse) {

            if (geomID == result->idTMQuery 
                || pDCD->params.tetrahedralTraverseForNonSelfIntersection)
            {
                ++result->numberOfTetTraversal;
                // query point traverse to closest point 
                //PathFinder::CPoint rayDirection = (closestP - qq);
                //pathFinder->markDesination(intersectionType, result->pMeshClosestElement);
                //bool hasValidTraverse = pathFinder->rayTMeshTraverse(result->pEmbraceTet, qq, rayDirection, closestP, intersectionType, 
                //   result->pMeshClosestElement, result->traversedTVec );
                //pathFinder->unmarkDesination(intersectionType, result->pMeshClosestElement);

                // closest point traverse to query point 
                // we move it a little bit to the center of the triangles to avoid intersecting with edges/vertices at the first tet

                Vec3fa closestPTracing;
                if (pointType == ClosestPointOnTriangleType::AtInterior)
                {
                    closestPTracing = closestP ;
                }
                else {
                    closestPTracing = closestP * (1.f - pDCD->params.centerShiftLevel) + (pDCD->params.centerShiftLevel / 3.0f) * (a + b + c);

                }

                Vec3fa targetPt;
                if (pDCD->params.shiftQueryPointToCenter)
                {
                    Vec3fa tetCentroid;
                    CuMatrix::tetCentroid(&tetCentroid.x, pTMSearch->mVertPos.data(), pTMSearch->mTetVIds.col(result->idEmbraceTet).data());

                    targetPt = (1.f - pDCD->params.centerShiftLevel) * queryPt + pDCD->params.centerShiftLevel * tetCentroid;
                }
                else {
                    targetPt = queryPt;
                }
                // traversing from the surface triangle to the query point
                Vec3fa rayDirection = (targetPt - closestPTracing);
                FloatingType rayLength = embree::length(rayDirection);
                rayDirection = rayDirection / rayLength;
                FloatingType maxSearchDis;
            
                if (pDCD->params.stopTraversingAfterPassingQueryPoint)
                {
                    maxSearchDis = pDCD->params.maxSearchDistanceMultiplier * rayLength;
                }
                else
                {
                    maxSearchDis = -1.f;
                }

                int32_t startingFaceId = pTMSearch->surfaceFacesIdAtBelongingTets(primID);
                int32_t startingTetId = pTMSearch->surfaceFacesBelongingTets(primID);

                Vec3 closestPTracingEigen;
                closestPTracingEigen << closestPTracing.x, closestPTracing.y, closestPTracing.z;

                Vec3 rayDirectionEigen;
                rayDirectionEigen << rayDirection.x, rayDirection.y, rayDirection.z;

                bool sucess = false;

                TraverseStatistics traverseStatistics;

    #ifdef OUTPUT_TRAVERSED_TETS 
                std::vector<int32_t> traversedTetsOutput;
    #endif
                if (pDCD->params.loopLessTraverse)
                {
                    sucess = pTMSearch->tetrahedralTraverseToLoopLess(closestPTracingEigen, rayDirectionEigen, maxSearchDis, startingTetId,
                        startingFaceId, result->idEmbraceTet, pDCD->params.rayTriIntersectionEPSILON, traverseStatistics);

                    if(!sucess && traverseStatistics.stopReason == TraverseStopReason::emptyStack) {
                        std::cout << "Empty stack (dead end) encountered!!! A ray is dicarded!!! \n";
                        std::cout << "Ray source: " << closestPTracingEigen.transpose() << " | ray target: " << queryPt << "\n";
                    }
                }
                else if (pDCD->params.useStaticTraverse)
                {
                    sucess = pTMSearch->tetrahedralTraverseTo(closestPTracingEigen, rayDirectionEigen, maxSearchDis, startingTetId,
                        startingFaceId, result->idEmbraceTet, pDCD->params.rayTriIntersectionEPSILON, traverseStatistics);

                    if (!sucess && traverseStatistics.stopReason == TraverseStopReason::emptyStack){
                        std::cout << "Empty stack (dead end) encountered!!! A ray is dicarded!!! \n";
                        std::cout << "Ray source: " << closestPTracingEigen.transpose() << " | ray target: " << queryPt << "\n";
                        //std::string outName =  "F:\\Projects\\Graphics\\P05_PBDDynamics_withRotator\\traversedTets" 
                        //    + std::to_string(startingTetId) + ".vtk";
                        //traversedTetsOutput.pop_back();
                        //pTMSearch->m_pTM_MF->vertPos() = pTMSearch->mVertPos.block(0,0,3, pTMSearch->numVertices());
                        //pTMSearch->m_pTM_MF->_write_tet_list_to_vtk(outName.c_str(), traversedTetsOutput);
                    }

                    if (!sucess && traverseStatistics.stopReason == TraverseStopReason::overflow)
                    {
                        std::cout << "Static traverse overflow!!! \n";
                        sucess = pTMSearch->tetrahedralTraverseToDynamic(closestPTracingEigen, rayDirectionEigen, maxSearchDis, startingTetId,
                            startingFaceId, result->idEmbraceTet, pDCD->params.rayTriIntersectionEPSILON, traverseStatistics);
                    }
                }
                else
                {
                    sucess = pTMSearch->tetrahedralTraverseToDynamic(closestPTracingEigen, rayDirectionEigen, maxSearchDis, startingTetId,
                        startingFaceId, result->idEmbraceTet, pDCD->params.rayTriIntersectionEPSILON, traverseStatistics);
                }

                result->numberOfTetsTraversed += traverseStatistics.numTetsTraversed;

                if (!sucess)
                {
                    return false;
                }
            }
        }


        args->query->radius = d;
        
        result->closestFaceId = primID;
        result->closestPt = closestP;
        result->closestPtBarycentrics = closestPtBarycentrics;

        result->closestPointType = pointType;
        // record that at least one closest point search has succeeded
        result->found = true;
        return true; // Return true to indicate that the query radius changed.
    }

    return false;
}

bool restPoseClosestPointQueryFunc(RTCPointQueryFunctionArguments* args)
{
#ifndef ENABLE_REST_POSE_CLOSEST_POINT
    std::cout << "Rest post closest point query called with rest pose query being disabled!!!" << std::endl;
    args->query->radius = 0.f;

    return true;
#endif // DEBUG

    ClosestPointQueryResult* result = (ClosestPointQueryResult*)args->userPtr;
    // TetMeshFEM* pTMQuery = result->pDCD->tMeshPtrs[result->idTMQuery].get();

    DiscreteCollisionDetector* pDCD = result->pDCD;

    assert(args->userPtr);
    const unsigned int geomID = args->geomID;
    const unsigned int primID = args->primID;
    TetMeshFEM* pTMSearch = result->pDCD->tMeshPtrs[geomID].get();

    embree::Vec3fa queryPt(args->query->x, args->query->y, args->query->z);

    embree::Vec3ia face(pTMSearch->surfaceFacesTetMeshVIds(0, primID), 
        pTMSearch->surfaceFacesTetMeshVIds(1, primID), pTMSearch->surfaceFacesTetMeshVIds(2, primID));

    embree::Vec3fa a = embree::Vec3fa::loadu(pTMSearch->restposeVerts.col(face[0]).data());
    embree::Vec3fa b = embree::Vec3fa::loadu(pTMSearch->restposeVerts.col(face[1]).data());
    embree::Vec3fa c = embree::Vec3fa::loadu(pTMSearch->restposeVerts.col(face[2]).data());

    ClosestPointOnTriangleType pointType;
    Vec3fa closestPtBarycentrics;
    Vec3fa closestP = SP::closestPointTriangle(queryPt, a, b, c, closestPtBarycentrics, pointType);
    float d = embree::distance(queryPt, closestP);
    
    if (d < args->query->radius)
        // no tet traverse is needed for rest pose query
    {
        args->query->radius = d;

        result->closestFaceId = primID;
        // compute back to deformed configuration
        embree::Vec3fa aD = embree::Vec3fa::loadu(pTMSearch->mVertPos.col(face[0]).data());
        embree::Vec3fa bD = embree::Vec3fa::loadu(pTMSearch->mVertPos.col(face[1]).data());
        embree::Vec3fa cD = embree::Vec3fa::loadu(pTMSearch->mVertPos.col(face[2]).data());
        result->closestPt = aD * closestPtBarycentrics[0]
            + bD * closestPtBarycentrics[1]
            + cD * closestPtBarycentrics[2];

        result->closestPtBarycentrics = closestPtBarycentrics;

        result->closestPointType = pointType;
        // record that at least one closest point search has succeeded
        result->found = true;

        return true; // Return true to indicate that the query radius changed.
    }

    return false;
}


SP::DiscreteCollisionDetector::DiscreteCollisionDetector(const CollisionDetectionParamters& in_params)
	: params(in_params)
{
	
}

void SP::DiscreteCollisionDetector::initialize(std::vector<std::shared_ptr<TetMeshFEM>> tMeshes)
{
	tMeshPtrs = tMeshes;

	device = rtcNewDevice(NULL);

	numTetsTotal = 0;

	// construct a separate scene for each surface mesh for shortest path query
	for (int meshId = 0; meshId < tMeshes.size(); meshId++)
	{
		RTCScene scene = rtcNewScene(device);
		RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);

        rtcSetSceneFlags(scene, RTC_SCENE_FLAG_DYNAMIC | RTC_SCENE_FLAG_ROBUST);
        rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_LOW);

        // use the existing buffer as Embree buffer
        if (params.restPoseCloestPoint)
        {
            rtcSetSharedGeometryBuffer(geom,
                RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, tMeshes[meshId]->restposeVerts.data(), 0, 3 * sizeof(float), 
                tMeshes[meshId]->numVertices());
            rtcSetGeometryPointQueryFunction(geom, restPoseClosestPointQueryFunc);
        }
        else {
            rtcSetSharedGeometryBuffer(geom,
                RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, tMeshes[meshId]->mVertPos.data(), 0, 3 * sizeof(float),
                tMeshes[meshId]->numVertices());
            rtcSetGeometryPointQueryFunction(geom, closestPointQueryFunc);
        }
    
        rtcSetSharedGeometryBuffer(geom,
            RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, tMeshes[meshId]->surfaceFacesTetMeshVIds.data(), 0, 3 * sizeof(unsigned),
            tMeshes[meshId]->numSurfaceFaces());

        rtcCommitGeometry(geom);
        rtcAttachGeometryByID(scene, geom, meshId);
        rtcReleaseGeometry(geom);
        rtcCommitScene(scene);

        surfaceMeshScenes.push_back(scene);
	}

    tetMeshesScene = rtcNewScene(device);
    rtcSetSceneFlags(tetMeshesScene, RTC_SCENE_FLAG_DYNAMIC | RTC_SCENE_FLAG_ROBUST);
    rtcSetSceneBuildQuality(tetMeshesScene, RTC_BUILD_QUALITY_LOW);
	// add all the tet mesh to a single scene for collision detection
	for (int meshId = 0; meshId < tMeshes.size(); meshId++)
	{
		RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_QUAD);

		rtcSetSharedGeometryBuffer(geom, 
			RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, tMeshes[meshId]->mVertPos.data(), 0, 3 * sizeof(float), tMeshes[meshId]->numVertices());

		rtcSetSharedGeometryBuffer(geom,
			RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT4, tMeshes[meshId]->mTetVIds.data(), 0, 4 * sizeof(unsigned), tMeshes[meshId]->numTets());

		rtcSetGeometryPointQueryFunction(geom, tetIntersectionFunc);

		rtcCommitGeometry(geom);
		unsigned int geomId = meshId;
		rtcAttachGeometryByID(tetMeshesScene, geom, geomId);
		//tetmeshGeoIdToPointerMap[geomId] = pTM;
		//tMId++;
		//tetmeshGeometryIds.push_back(geomId);
		rtcReleaseGeometry(geom);
	}
    rtcCommitScene(tetMeshesScene);

}

void SP::DiscreteCollisionDetector::updateBVH(RTCBuildQuality tetMeshSceneQuality, 
    RTCBuildQuality surfaceSceneQuality, bool updateSurfaceScene)
{

    RTCBuildQuality tetMeshGeomQuality = tetMeshSceneQuality;
    if (tetMeshSceneQuality == RTC_BUILD_QUALITY_REFIT) {
        tetMeshSceneQuality = RTC_BUILD_QUALITY_LOW;
    }

    RTCBuildQuality surfaceGeomQuality = surfaceSceneQuality;
    if (surfaceSceneQuality == RTC_BUILD_QUALITY_REFIT) {
        surfaceSceneQuality = RTC_BUILD_QUALITY_LOW;
    }

    rtcSetSceneBuildQuality(tetMeshesScene, tetMeshSceneQuality);

    for (size_t iMesh = 0; iMesh < tMeshPtrs.size(); iMesh++)
    {

        // get the tet geom buffer
        unsigned int geoId = iMesh;
        TetMeshFEM* pTM = tMeshPtrs[iMesh].get();
        RTCGeometry geom = rtcGetGeometry(tetMeshesScene, geoId);

        if (pTM->activeForCollision) {
            rtcEnableGeometry(geom);
        }
        else
        {
            rtcDisableGeometry(geom);
            continue;
        }

        rtcSetGeometryBuildQuality(geom, tetMeshGeomQuality);

        //rtcUpdateGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0);
        //rtcCommitGeometry(geom);

        rtcUpdateGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0);
        rtcCommitGeometry(geom);
     
        if (updateSurfaceScene && !params.restPoseCloestPoint) {
            // update surface Mesh
            RTCScene surfaceScene = surfaceMeshScenes[iMesh];
            rtcSetSceneBuildQuality(surfaceScene, surfaceSceneQuality);

            RTCGeometry geomSurface = rtcGetGeometry(surfaceScene, iMesh);
            rtcSetGeometryBuildQuality(geomSurface, surfaceGeomQuality);

            rtcUpdateGeometryBuffer(geomSurface, RTC_BUFFER_TYPE_VERTEX, 0);
            rtcCommitGeometry(geomSurface);
            rtcCommitScene(surfaceScene);
        }
    }

    rtcCommitScene(tetMeshesScene);
}

bool SP::DiscreteCollisionDetector::vertexCollisionDetection(int32_t vId, int32_t tMeshId, CollisionDetectionResult* pResult)
{
    RTCPointQueryContext context;
    rtcInitPointQueryContext(&context);
    TetMeshFEM* pTM = tMeshPtrs[tMeshId].get();
    RTCPointQuery query;
    query.x = pTM->mVertPos(0, vId);
    query.y = pTM->mVertPos(1, vId);
    query.z = pTM->mVertPos(2, vId);
    query.radius = 0.f;
    query.time = 0.f;

    pResult->clear();

    pResult->idTMQuery = tMeshId;
    pResult->idVQuery = vId;
    pResult->pDetector = (void*)this;
    pResult->handleSelfIntersection = params.handleSelfCollision;

    rtcPointQuery(tetMeshesScene, &query, &context, nullptr, (void*)pResult);
    return true;
}

bool SP::DiscreteCollisionDetector::closestPointQuery(CollisionDetectionResult* pColResult, ClosestPointQueryResult* pClosestPtResult, bool computeClosestPointNormal)
{
    TetMeshFEM* pTM = tMeshPtrs[pColResult->idTMQuery].get();
    RTCPointQuery query;


    query.x = pTM->mVertPos(0, pColResult->idVQuery);
    query.y = pTM->mVertPos(1, pColResult->idVQuery);
    query.z = pTM->mVertPos(2, pColResult->idVQuery);

    pClosestPtResult->pDCD = this;
    pClosestPtResult->idVQuery = pColResult->idVQuery;
    pClosestPtResult->idTMQuery = pColResult->idTMQuery;

    pClosestPtResult->checkFeasibleRegion = params.checkFeasibleRegion;
    pClosestPtResult->checkTetTraverse = params.checkTetTraverse;

    for (int  iIntersection = 0;  iIntersection < pColResult->intersectedTets.size();  iIntersection++)
    {
        query.radius = embree::inf;
        query.time = 0.f;

        int idTMIntersected = pColResult->intersectedTMeshIds[iIntersection];
        int idTetIntersected = pColResult->intersectedTets[iIntersection];

        if (params.restPoseCloestPoint)
        {
            TetMeshFEM* pTMSearch = tMeshPtrs[idTMIntersected].get();

            // compute the barycenters in the embracing tet
            float barycentricsEmbracingTet[4];
            CuMatrix::tetPointBarycentricsInTet(pTM->mVertPos.col(pColResult->idVQuery).data(), pTMSearch->mVertPos.data(),
                pTMSearch->mTetVIds.col(idTetIntersected).data(), barycentricsEmbracingTet);
            // map back to rest pose position
            embree::Vec3fa queryPt(0.f, 0.f, 0.f);
            for (size_t iV = 0; iV < 4; iV++)
            {
                int32_t vId = pTMSearch->mTetVIds(iV, idTetIntersected);
                embree::Vec3fa restposeP = embree::Vec3fa::loadu(pTMSearch->restposeVerts.col(vId).data());
                queryPt += restposeP * barycentricsEmbracingTet[iV];

            }
            query.x = queryPt.x;
            query.y = queryPt.y;
            query.z = queryPt.z;
        }

        pClosestPtResult->idEmbraceTet = idTetIntersected;

        pClosestPtResult->found = false;;
        pClosestPtResult->closestPointType = ClosestPointOnTriangleType::NotFound;

        RTCPointQueryContext context;
        rtcInitPointQueryContext(&context);
        rtcPointQuery(surfaceMeshScenes[idTMIntersected], &query, &context, nullptr, (void*)pClosestPtResult);

        // for testing
        //queryPoint(closestPtResult, p, pTetIntersected, surfaceSceneId, inf);
        //CPoint closestP1 = closestPtResult.closestP;
        //queryPoint(closestPtResult, p, pTetIntersected, surfaceSceneId, inf, false, false);
        //CPoint closestP2 = closestPtResult.closestP;

        //assert((closestP2 - closestP1).norm() < 1e-6);

        if (pClosestPtResult->found) {
            // pColResult->closestPoints.push_back(closestPtResult.closestP);
            //std::array<float, 3> closestPtBarycentrics = {
            //    pClosestPtResult->closestPtBarycentrics.x,
            //    pClosestPtResult->closestPtBarycentrics.y,
            //    pClosestPtResult->closestPtBarycentrics.z,
            //};
            //std::array<float, 3> closestPt = {
            //    pClosestPtResult->closestPt.x,
            //    pClosestPtResult->closestPt.y,
            //    pClosestPtResult->closestPt.z,
            //};
            pColResult->shortestPathFound.push_back(true);
            pColResult->closestSurfacePtBarycentrics.push_back({
                pClosestPtResult->closestPtBarycentrics.x,
                pClosestPtResult->closestPtBarycentrics.y,
                pClosestPtResult->closestPtBarycentrics.z,
            });
            pColResult->closestSurfacePts.push_back({
                pClosestPtResult->closestPt.x,
                pClosestPtResult->closestPt.y,
                pClosestPtResult->closestPt.z,
            });
            pColResult->closestSurfaceFaceId.push_back(pClosestPtResult->closestFaceId);
            pColResult->closestPointType.push_back(pClosestPtResult->closestPointType);

            pColResult->numberOfBVHQuery += pClosestPtResult->numberOfBVHQuery;
            pColResult->numberOfTetsTraversed += pClosestPtResult->numberOfTetsTraversed;
            pColResult->numberOfTetTraversal += pClosestPtResult->numberOfTetTraversal;

            if (computeClosestPointNormal)
            {
                std::array<float, 3> normalOut;
                computeNormal(*pColResult, iIntersection, normalOut);
                pColResult->closestPointNormals.push_back(normalOut);
            }
        }
        else
        {
            pColResult->shortestPathFound.push_back(false);
            pColResult->closestSurfacePtBarycentrics.push_back({ -1.f, -1.f, -1.f });
            pColResult->closestSurfacePts.push_back({ -1.f, -1.f, -1.f });
            pColResult->closestSurfaceFaceId.push_back(-1);
            pColResult->closestPointType.push_back(ClosestPointOnTriangleType::NotFound);
        //    // std::cout << "fail to find closest path!\n";
            if (computeClosestPointNormal)
            {
                pColResult->closestPointNormals.push_back({0.f, 0.f, 0.f});
            }
        }


    }

    return true;
}

bool SP::DiscreteCollisionDetector::checkFeasibleRegion(embree::Vec3fa& p, TetMeshFEM* pTM, int32_t faceId,
    ClosestPointOnTriangleType pointType, float feasibleRegionEpsilon)
{
    bool inFeasibleRegion = true;
    int32_t* faceVIds = pTM->surfaceFacesTetMeshVIds.col(faceId).data();
    switch (pointType)
    {
    case ClosestPointOnTriangleType::AtInterior:
        // this is automatically satisfied
        inFeasibleRegion = true;
        break;
    case ClosestPointOnTriangleType::AtAB:
        inFeasibleRegion = checkEdgeFeasibleRegion(p, pTM, faceId, 0, faceVIds[0], faceVIds[1], feasibleRegionEpsilon);
        break;
    case ClosestPointOnTriangleType::AtBC:
        inFeasibleRegion = checkEdgeFeasibleRegion(p, pTM, faceId, 1, faceVIds[1], faceVIds[2], feasibleRegionEpsilon);
        break;
    case ClosestPointOnTriangleType::AtAC:
        inFeasibleRegion = checkEdgeFeasibleRegion(p, pTM, faceId, 2, faceVIds[2], faceVIds[0], feasibleRegionEpsilon);
        break;
    case ClosestPointOnTriangleType::AtA:
        inFeasibleRegion = checkVertexFeasibleRegion(p, pTM, faceVIds[0], feasibleRegionEpsilon);
        break;
    case ClosestPointOnTriangleType::AtB:
        inFeasibleRegion = checkVertexFeasibleRegion(p, pTM, faceVIds[1], feasibleRegionEpsilon);
        break;
    case ClosestPointOnTriangleType::AtC:
        inFeasibleRegion = checkVertexFeasibleRegion(p, pTM, faceVIds[2], feasibleRegionEpsilon);
        break;
    case ClosestPointOnTriangleType::NotFound:
        inFeasibleRegion = false;
        break;
    default:
        break;
    }
    return inFeasibleRegion;
}

void SP::DiscreteCollisionDetector::computeNormal(CollisionDetectionResult& colResult, int32_t iIntersection, std::array<float, 3>& normalOut)
{
    ClosestPointOnTriangleType pointType = colResult.closestPointType[iIntersection];
    int32_t curVertID = colResult.idVQuery;
    int32_t curMeshID = colResult.idTMQuery;

    int32_t collidedMeshID = colResult.intersectedTMeshIds[iIntersection];
    int32_t closestFaceId = colResult.closestSurfaceFaceId[iIntersection];
    TetMeshFEM* pCurTM = tMeshPtrs[curMeshID].get();
    TetMeshFEM* pIntersectedTM = tMeshPtrs[collidedMeshID].get();

    Vec3 normal;
    int32_t surfaceVIdTMeshIndex = -1;
    int32_t surfaceVIdSurfaceIndex = -1;

    switch (pointType)
    {
    case SP::ClosestPointOnTriangleType::AtA:
        surfaceVIdTMeshIndex = pIntersectedTM->surfaceFacesTetMeshVIds(0, closestFaceId);
        surfaceVIdSurfaceIndex = pIntersectedTM->surfaceFacesSurfaceMeshVIds(0, closestFaceId);
        pIntersectedTM->computeVertexNormal(surfaceVIdSurfaceIndex, normal);

        break;
    case SP::ClosestPointOnTriangleType::AtB:
        surfaceVIdTMeshIndex = pIntersectedTM->surfaceFacesTetMeshVIds(1, closestFaceId);
        surfaceVIdSurfaceIndex = pIntersectedTM->surfaceFacesSurfaceMeshVIds(1, closestFaceId);
        pIntersectedTM->computeVertexNormal(surfaceVIdSurfaceIndex, normal);
        break;
    case SP::ClosestPointOnTriangleType::AtC:
        surfaceVIdTMeshIndex = pIntersectedTM->surfaceFacesTetMeshVIds(2, closestFaceId);
        surfaceVIdSurfaceIndex = pIntersectedTM->surfaceFacesSurfaceMeshVIds(2, closestFaceId);
        pIntersectedTM->computeVertexNormal(surfaceVIdSurfaceIndex, normal);
        break;
    case SP::ClosestPointOnTriangleType::AtAB:
        pIntersectedTM->computeEdgeNormal(closestFaceId, 0, normal);
        break;
    case SP::ClosestPointOnTriangleType::AtBC:
        pIntersectedTM->computeEdgeNormal(closestFaceId, 1, normal);
        break;
    case SP::ClosestPointOnTriangleType::AtAC:
        pIntersectedTM->computeEdgeNormal(closestFaceId, 2, normal);
        break;
    case SP::ClosestPointOnTriangleType::AtInterior:
        pIntersectedTM->computeFaceNormal(closestFaceId, normal);
        break;
    case SP::ClosestPointOnTriangleType::NotFound:
        return;
        break;
    default:
        return;
        break;
    }
    normalOut[0] = normal(0);
    normalOut[1] = normal(1);
    normalOut[2] = normal(2);
}



bool SP::checkEdgeFeasibleRegion(embree::Vec3fa& p, TetMeshFEM* pTM, int32_t faceId, int32_t edgeId, int32_t edgeVId1, int32_t edgeVId2, float feasibleRegionEpsilon)
{
    //M::HEPtr pHE1 = M::edgeHalfedge(pE);
    //M::HEPtr pHE2 = M::halfedgeSym(M::edgeHalfedge(pE));

    //M::FPtr pF1 = M::halfedgeFace(pHE1);
    //M::FPtr pF2 = M::halfedgeFace(pHE2);

    int32_t neighborFaceId = pTM->surfaceFaces3NeighborFaces(edgeId);

    embree::Vec3fa v1 = loadVertexPos(pTM, edgeVId1);
    embree::Vec3fa v2 = loadVertexPos(pTM, edgeVId2);

    // skip feasible region filtering for inverted surface parts
    /*if (   !pTetM->DCDEnabled(pM->edgeVertex1(pE)->pTetMeshV)
        || !pTetM->DCDEnabled(pM->edgeVertex2(pE)->pTetMeshV)
        || !pTetM->DCDEnabled(pM->halfedgeOppositeVertex(pHE1)->pTetMeshV)
        || !pTetM->DCDEnabled(pM->halfedgeOppositeVertex(pHE2)->pTetMeshV)
        )
    {
        return true;
    }*/

    if (neighborFaceId == -1) {
        assert(false);
        printf("Boundary edge encountered! The mesh is supposed to be water tight.\n");
        return true;
    }

    // we are looking form the inside of the mesh, thus the face normal shoud be inverted
    Vec3fa fNormal1 = -faceNormal(pTM, faceId);
    Vec3fa fNormal2 = -faceNormal(pTM, neighborFaceId);

    Vec3fa AP = p - v1;
    Vec3fa BP = p - v2;
    Vec3fa AB = v2 - v1;
    Vec3fa BA = -AB;

    float ABNorm2 = embree::sqr_length(AB);
    //float APNorm2 = embree::sqr_length(AP);

    float relaxed = -( ABNorm2) * feasibleRegionEpsilon - ABSOLUTE_RELAXIATION;

    if (embree::dot(AP, AB) < relaxed) {
        return false;
    }

    //CPoint BP = p - B;

    if (embree::dot(BP, BA) < relaxed) {
        return false;
    }

    Vec3fa nAB = cross(fNormal1, AB);
    if (embree::dot(AP, nAB) < relaxed) {
        return false;
    }

    Vec3fa nBA = cross(fNormal2, BA);
    if (embree::dot(AP, nBA) < relaxed) {
        return false;
    }

    return true;
}

bool SP::checkVertexFeasibleRegion(embree::Vec3fa& p, TetMeshFEM* pTM, int32_t vId, float feasibleRegionEpsilon)
{
    // skip feasible region filtering for inverted surface parts
    // if (!pTetM->DCDEnabled(pV->pTetMeshV))
    // {
    //     return true;
    // }

    Vec3fa A = loadVertexPos(pTM, vId);;

    Vec3fa AP = p - A;
    float APNorm2 = embree::sqr_length(AP);
    //CPoint APNormalized = AP / APNorm;

    int32_t surfaceVId = pTM->tetVertIndicesToSurfaceVertIndices(vId);
    assert(surfaceVId != -1);
    for (int32_t iVNei = 0; iVNei < pTM->surfaceVertexNeighborSurfaceVertices[surfaceVId].size(); ++iVNei) {
        int32_t neiVId = pTM->surfaceVertexNeighborSurfaceVertices[surfaceVId][iVNei];
        Vec3fa B = loadVertexPos(pTM, neiVId);;

        Vec3fa BA = A - B;

        //// skip feasible region filtering for inverted surface parts
        //if (!pTetM->DCDEnabled(pVNei->pTetMeshV))
        //{
        //    return true;
        //}

        float relaxed = -(embree::dot(BA, BA)) * feasibleRegionEpsilon - ABSOLUTE_RELAXIATION;

        // AP  * BA > 0 means P is on the demanded side of plane passing through A whose normal is BA 
        // add a little margin to make the determination more conservative: 
        // instead of AP * BA <= 0 we use AP * BA <= epsilon, where epsilon < 0
        if (embree::dot(AP, BA) < relaxed) {
            return false;
        }
    }

    return true;
}
